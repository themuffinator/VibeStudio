#pragma once

// The Texture Generator: a prompt (or a picture) in, game-ready textures out
// (core/texture_generation.h), drawn by the configured image model. Variants
// are reviewed tiled, so seams show, with their palette and companion maps,
// before one is saved where the game reads it, applied to the map, or opened
// in the Texture Editor. The shell supplies palettes, consent, and the other
// surfaces through hooks.

#include "app/texture_preview_worker.h"
#include "core/ai_image_transport.h"
#include "core/texture_generation.h"

#include <QCoreApplication>
#include <QDialog>
#include <QJsonObject>
#include <QVector>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace vibestudio {

struct TextureGenerationDialogHooks {
	// The game the open package, map, or project targets, or empty.
	std::function<QString()> defaultGame;
	// Where the game's palette can be read; resolved off the UI thread.
	std::function<TexturePreviewSource()> paletteSource;
	// The folder textures are written into.
	std::function<QString()> outputFolder;
	std::function<bool(const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request)> confirmSend;
	// The texture open on the Textures page, to restyle; null when none.
	std::function<QImage()> currentTexture;
	std::function<void(const QImage& image, const QString& name)> openInEditor;
	// Applies a saved texture's map name to the selected faces; false says why.
	std::function<bool(const QString& mapTextureName, QString* error)> applyToMap;
	std::function<void(const QStringList& paths)> written;
	// Texture names the open map uses that nothing provides, from its texture
	// audit; one picked is made under the name the map already uses.
	std::function<QStringList()> missingTextures;
	std::function<QString(const QString& title, const QString& detail)> beginTask;
	std::function<void(const QString& task, bool succeeded, bool cancelled, const QString& summary)> endTask;
};

class TextureGenerationDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioTextureGenerationDialog)

public:
	TextureGenerationDialog(QWidget* parent, TextureGenerationDialogHooks hooks);
	~TextureGenerationDialog() override;

	void setPrompt(const QString& prompt);
	void generate();
	void cancelGeneration();
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const QVector<GeneratedTexture>& textures() const;
	// Saves the selected variant; with `apply`, puts it on the map selection.
	bool saveSelected(bool apply);
	void refreshSourceStatus();

protected:
	void reject() override;

private:
	struct Work;

	TextureGenerationSpec specFromControls() const;
	void process(const TextureGenerationSpec& spec, const QVector<QImage>& pictures, const QJsonObject& provenance, const QStringList& notes);
	void showTextures(const QStringList& notes);
	void showSelected();
	void setBusy(bool busy, const QString& status);
	void finishTask(bool succeeded, bool cancelled, const QString& summary);
	void updateGameControls();
	void showRequestPreview();
	void refreshMissingTextures();
	void useMissingTexture(const QString& name);
	AiImageRequest imageRequest(const TextureGenerationSpec& spec, const AiImageConnection& connection) const;

	TextureGenerationDialogHooks m_hooks;
	std::shared_ptr<Work> m_work;
	std::unique_ptr<AiImageClient> m_client;
	QVector<GeneratedTexture> m_textures;
	QVector<TextureGenerationSpec> m_specs;
	QJsonObject m_provenance;
	QString m_task;
	bool m_busy = false;

	QPlainTextEdit* m_prompt = nullptr;
	QComboBox* m_game = nullptr;
	QComboBox* m_surface = nullptr;
	QLineEdit* m_style = nullptr;
	QComboBox* m_size = nullptr;
	QComboBox* m_missing = nullptr;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_directory = nullptr;
	QLineEdit* m_wad = nullptr;
	QSpinBox* m_variants = nullptr;
	QCheckBox* m_seamless = nullptr;
	QCheckBox* m_companions = nullptr;
	QCheckBox* m_dither = nullptr;
	QCheckBox* m_fullbrights = nullptr;
	QRadioButton* m_modelSource = nullptr;
	QRadioButton* m_pictureSource = nullptr;
	QLabel* m_sourceStatus = nullptr;
	QCheckBox* m_restyle = nullptr;
	QLineEdit* m_picture = nullptr;
	QPushButton* m_browsePicture = nullptr;
	QPushButton* m_previewRequest = nullptr;
	QPushButton* m_generate = nullptr;
	QPushButton* m_cancel = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QListWidget* m_variantList = nullptr;
	QLabel* m_companionStrip = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton* m_save = nullptr;
	QPushButton* m_saveApply = nullptr;
	QPushButton* m_openEditor = nullptr;
};

} // namespace vibestudio
