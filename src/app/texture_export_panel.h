#pragma once

#include "core/texture_export.h"

#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace vibestudio {

class TextureExportPanel final : public QWidget {
public:
	explicit TextureExportPanel(QWidget* parent = nullptr);
	std::function<void()> changed, previewRequested, exportRequested;
	[[nodiscard]] QJsonObject settings() const;
	bool options(TextureExportOptions* options, QString* error = nullptr) const;
	void setOptions(const TextureExportOptions& options);
	void showResult(const TextureExportResult& result, const IdTechPaletteResolution& palette);
	void invalidatePreview();
	[[nodiscard]] QImage previewImage() const;
protected:
	void resizeEvent(QResizeEvent* event) override;
private:
	void updateFields();
	void updatePreview();
	QFormLayout* m_form = nullptr;
	QComboBox *m_profile = nullptr, *m_alpha = nullptr, *m_filter = nullptr, *m_fullbright = nullptr, *m_mip = nullptr;
	QLineEdit *m_matte = nullptr, *m_name = nullptr, *m_animation = nullptr, *m_flags = nullptr, *m_contents = nullptr;
	QSpinBox *m_threshold = nullptr, *m_value = nullptr, *m_left = nullptr, *m_top = nullptr;
	QCheckBox *m_dither = nullptr, *m_generated = nullptr, *m_extended = nullptr;
	QLabel *m_description = nullptr, *m_preview = nullptr, *m_report = nullptr;
	QVector<QImage> m_images;
	bool m_loading = false;
};

} // namespace vibestudio
