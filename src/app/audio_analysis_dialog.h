#pragma once

#include "core/audio_analysis.h"

#include <QDialog>

class QComboBox;
class QCheckBox;

namespace vibestudio
{

class AudioChannelMapDialog final : public QDialog
{
	Q_OBJECT
public:
	explicit AudioChannelMapDialog(int channels, QWidget* parent = nullptr);
	[[nodiscard]] AudioAnalysisOptions options() const;
private:
	QCheckBox* m_loudness = nullptr;
	QVector<QComboBox*> m_roles;
};

class AudioAnalysisDialog final : public QDialog
{
	Q_OBJECT
public:
	explicit AudioAnalysisDialog(AudioAnalysis analysis, const QString& source, QWidget* parent = nullptr);
	[[nodiscard]] const AudioAnalysis& analysis() const { return m_analysis; }

private:
	AudioAnalysis m_analysis;
};

} // namespace vibestudio
