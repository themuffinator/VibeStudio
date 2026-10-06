#pragma once
#include "core/editor_profiles.h"
#include <QDialog>
#include <QHash>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QPushButton;
class QKeySequenceEdit;
class QPlainTextEdit;
class QToolButton;
namespace vibestudio {
class StudioSettings;
class LevelGesturesDialog final : public QDialog {
	Q_OBJECT
public:
	LevelGesturesDialog(StudioSettings& settings, const QString& profile, QWidget* parent = nullptr);
	QJsonObject draft() const;
	bool importFile(const QString& path, QString* error = nullptr);
	bool exportFile(const QString& path, bool overwrite, QString* error = nullptr) const;
Q_SIGNALS:
	void preferencesApplied();
private:
	void setDraft(const QJsonObject& overrides);
	void validateDraft();
	void setStatus(const QString& text);
	StudioSettings& m_settings;
	EditorProfileDescriptor m_profile;
	QHash<QString, QComboBox*> m_fields;
	QHash<QString, QKeySequenceEdit*> m_keyEdits;
	QPlainTextEdit* m_overlaps = nullptr;
	QToolButton* m_showOverlaps = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_apply = nullptr;
	QPushButton* m_export = nullptr;
	QString m_loadError;
};
} // namespace vibestudio
