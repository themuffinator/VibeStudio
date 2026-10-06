#pragma once

#include "core/code_files.h"
#include <QHash>
#include <QSet>
#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace vibestudio {

class CodeFilesWorker;

// The worker owns filesystem access. This panel applies a bounded result in
// small event-loop batches and filters only its metadata snapshot.
class CodeFilesPanel final : public QWidget {
	Q_OBJECT
public:
	explicit CodeFilesPanel(QTreeWidget* tree, QLineEdit* filter, QWidget* parent = nullptr);
	void setRootPath(const QString& root, bool force = false);
	void refresh();
	void cancel();
	void setCurrentPath(const QString& path);
	void filterFiles();
	bool busy() const { return m_busy; }
	const CodeFilesResult& result() const { return m_result; }
	std::function<void()> operationStarted;
	std::function<void(int files, int entries)> operationProgress;
	std::function<void(const CodeFilesResult&)> operationFinished;

private:
	void retire();
	void applyResult(const CodeFilesResult& result);
	void appendRows();
	void finish();
	void followCurrentPath();
	void setStatus(const QString& text);
	void placeholder(const QString& text);
	QString m_root;
	QString m_currentPath;
	QString m_selectedPath;
	CodeFilesResult m_result;
	bool m_requested = false;
	bool m_busy = false;
	int m_nextFile = 0;
	QSet<QString> m_expanded;
	QHash<QString, QTreeWidgetItem*> m_directories;
	QHash<QString, StudioQueryProperties> m_properties;
	CodeFilesWorker* m_worker = nullptr;
	QTreeWidget* m_tree = nullptr;
	QLineEdit* m_filter = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_details = nullptr;
	QLabel* m_queryStatus = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_cancel = nullptr;
	QTimer* m_rowsTimer = nullptr;
};

} // namespace vibestudio
