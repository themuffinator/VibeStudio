#pragma once

#include "core/level_navigation.h"
#include <QCoreApplication>
#include <QDialog>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;

namespace vibestudio {
class LevelBookmarksDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelBookmarksDialog)
public:
	using Capture = std::function<bool(LevelViewState*, QString*)>;
	using Restore = std::function<bool(const LevelViewState&, QString*)>;
	using Commit = std::function<bool(const LevelViewBookmarks&, QString*)>;
	using Reload = std::function<bool(LevelViewBookmarks*, QString*)>;
	LevelBookmarksDialog(LevelViewBookmarks bookmarks, Capture capture, Restore restore, Commit commit, Reload reload,
		bool readOnly, QWidget* parent = nullptr);
	[[nodiscard]] const LevelViewBookmarks& bookmarks() const { return m_bookmarks; }
	bool importFile(const QString& path);
	bool exportFile(const QString& path, bool overwrite);
	void setStatusMessage(const QString& message) { report(message); }
private:
	void refresh(const QString& selected = {});
	void report(const QString& error);
	void change(int operation);
	LevelViewBookmarks m_bookmarks;
	Capture m_capture;
	Restore m_restore;
	Commit m_commit;
	Reload m_reload;
	QListWidget* m_list = nullptr;
	QLineEdit* m_name = nullptr;
	QLabel* m_detail = nullptr;
	QLabel* m_status = nullptr;
	bool m_readOnly = false;
};
} // namespace vibestudio
