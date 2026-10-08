#pragma once

#include "core/level_map.h"
#include <QWidget>
#include <functional>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace vibestudio {
class LevelScenePanel final : public QWidget {
	Q_OBJECT
  public:
	explicit LevelScenePanel(LevelMapDocument* document, QWidget* parent = nullptr);
	void refresh();
	void refreshSelection();
  Q_SIGNALS:
	void sceneChanged();
	void selectionChanged();

  private:
	QString currentId() const;
	bool current() const;
	void edit(const std::function<bool(QString*)>& operation);
	void updateControls();
	LevelMapDocument* m_document = nullptr;
	quint64 m_revision = 0;
	QString m_source, m_mapName;
	QByteArray m_hash;
	bool m_refreshing = false;
	QTreeWidget* m_tree = nullptr;
	QLineEdit* m_name = nullptr;
	QComboBox* m_parent = nullptr;
	QComboBox* m_creation = nullptr;
	QCheckBox* m_lock = nullptr;
	QLabel* m_status = nullptr;
	QWidget* m_editControls = nullptr;
	QPushButton* m_rename = nullptr;
	QPushButton* m_move = nullptr;
	QPushButton* m_remove = nullptr;
	QPushButton* m_assign = nullptr;
	QPushButton* m_reset = nullptr;
	// Linked copies of the chosen group (core/level_linked_groups.h).
	QPushButton* m_linkCopy = nullptr;
	QPushButton* m_unlink = nullptr;
	QPushButton* m_updateLinks = nullptr;
	QPushButton* m_selectLinks = nullptr;
};
} // namespace vibestudio
