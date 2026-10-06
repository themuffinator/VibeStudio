#pragma once

#include "core/language_server.h"
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QTimer;

namespace vibestudio {

class CodeLanguagePanel final : public QWidget {
	Q_OBJECT
public:
	explicit CodeLanguagePanel(QWidget* parent = nullptr);
	LanguageServerClient* client() const { return m_client; }
	void setRootPath(const QString& root);
	void setPreferences(const QJsonObject& preferences);
	QJsonObject preferences() const;
	void connectServer();
	void scheduleSync();
	bool synchronizeNow();
	void documentSaved(const QString& path, const QString& text);
	std::function<QVector<LanguageDocument>(const QString& language, const QStringList& extensions, QString* error)> snapshots;
	std::function<void(const QJsonObject&)> preferencesChanged;
	std::function<void()> connectionChanged;
private:
	QPushButton* m_refreshDiagnostics = nullptr;
	void refresh();
	LanguageServerClient* m_client;
	QString m_root;
	QString m_syncError;
	QLineEdit *m_program, *m_language, *m_extensions;
	QPlainTextEdit *m_arguments, *m_log;
	QLabel* m_status;
	QPushButton *m_connect, *m_disconnect, *m_browse;
	QProgressBar* m_progress;
	QTimer *m_syncTimer, *m_refreshTimer;
};

} // namespace vibestudio
