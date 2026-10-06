#pragma once

#include "core/audio_level.h"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

namespace vibestudio
{
class AudioPlacementDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioPlacementDialog(const LevelSoundTarget& target, const QString& mapName, const QString& packageName,
	                     const LevelSoundRequest& initial, QWidget* parent = nullptr);
	[[nodiscard]] LevelSoundRequest request() const;

  private:
	QString m_path;
	QComboBox* m_game = nullptr;
	QComboBox* m_mode = nullptr;
	QLineEdit* m_targetName = nullptr;
	QDoubleSpinBox* m_position[3]{};
};
} // namespace vibestudio
