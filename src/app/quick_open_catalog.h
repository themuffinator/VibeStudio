#pragma once

#include "core/code_files.h"
#include "core/package_archive.h"

#include <QObject>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio {

// Keys are absolute file paths, or "package:" followed by a virtual path.
struct QuickOpenEntry {
	QString key;
	QString name;
	QString folder;
	QString source;
};

struct QuickOpenRequest {
	QString rootPath;
	QStringList recentPaths;
	QString packageSource;
	QVector<PackageEntry> packageEntries;
	int maxProjectFiles = 20000;
	int maxPackageEntries = 100000;
};

struct QuickOpenResult {
	QVector<QuickOpenEntry> entries;
	OperationState state = OperationState::Idle;
	QStringList warnings;
	QString error;
};

// Recent labels require no filesystem access; the worker validates availability.
QVector<QuickOpenEntry> quickOpenRecentEntries(const QStringList& paths);

class QuickOpenCatalog final : public QObject {
public:
	explicit QuickOpenCatalog(QObject* parent = nullptr);
	~QuickOpenCatalog() override;
	void start(QuickOpenRequest request);
	void cancel();
	void reset();
	bool busy() const { return m_thread || m_pending.has_value(); }
	std::function<void(int files, int entries)> progress;
	std::function<void(const QuickOpenResult&)> completed;

private:
	struct Work;
	void startPending();
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	std::shared_ptr<Work> m_work;
	std::optional<QuickOpenRequest> m_pending;
	quint64 m_serial = 0;
};

} // namespace vibestudio
