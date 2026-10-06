#pragma once

#include "core/level_document.h"

#include <QCoreApplication>
#include <QDialog>
#include <QObject>
#include <QSet>
#include <memory>

class QComboBox;
class QLineEdit;
class QThread;

namespace vibestudio
{

class NewLevelMapDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(NewLevelMapDialog)
public:
	explicit NewLevelMapDialog(QWidget* parent = nullptr);
	LevelMapCreateRequest request() const;

private:
	QComboBox* m_game = nullptr;
	QComboBox* m_preset = nullptr;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_marker = nullptr;
	QLineEdit* m_wall = nullptr;
	QLineEdit* m_floor = nullptr;
	QLineEdit* m_ceiling = nullptr;
};

// Snapshots are written off the UI thread. Retiring a document also retires any
// in-flight write; an old worker can never recreate a discarded checkpoint.
class LevelRecoveryWriter final : public QObject
{
public:
	explicit LevelRecoveryWriter(QString directory, QObject* parent = nullptr);
	~LevelRecoveryWriter() override;
	void retire();
	bool checkpoint(const LevelMapDocument& document);
	QString directory() const { return m_directory; }
	QString documentId() const { return m_id; }
	bool busy() const { return m_thread != nullptr; }
	std::function<void(const QString& path, const QString& error)> finished;

private:
	struct Result;
	void removeRetired();
	QString m_directory;
	QString m_id;
	QSet<QString> m_retired;
	QThread* m_thread = nullptr;
	std::shared_ptr<Result> m_result;
	quint64 m_savedRevision = 0;
	bool m_hasCheckpoint = false;
};

QString levelMapRecoveryDirectory();
// Returns the selected valid checkpoint, or an empty path on cancellation.
QString chooseLevelMapRecovery(QWidget* parent, const QString& directory);

} // namespace vibestudio
