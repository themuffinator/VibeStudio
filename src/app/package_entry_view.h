#pragma once

#include "core/package_browser.h"

#include <QListView>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio {

class PackageEntryModel;
QString packageEntryIconName(const PackageEntry& entry);

// Only visible row presentation is materialized. One cancellable worker and
// one replaceable pending request keep fast typing/navigation bounded.
class PackageEntryView final : public QListView {
	Q_OBJECT
public:
	explicit PackageEntryView(QWidget* parent = nullptr);
	~PackageEntryView() override;
	// Optional diagnostics/cancellation callbacks run on the worker.
	void showEntries(const PackageArchive& archive, const QString& revision, const QString& folder, const QString& query,
		const PackageReadControl& control = {});
	void showMessage(const QString& message, const QString& detail = {});
	void cancel();
	void retry();
	void selectEntry(const QString& path, qint64 ordinal = -1);
	void selectAllWhenReady();
	[[nodiscard]] bool busy() const { return m_busy; }
	[[nodiscard]] bool canRetry() const { return m_retry; }
	[[nodiscard]] bool readyFor(const QString& revision) const;
	// Shared metadata stays available while a query on this same revision is
	// filtering or cancelled. A new revision never exposes the old index.
	[[nodiscard]] std::shared_ptr<const PackageBrowserIndex> browserIndex(const QString& revision) const;
	[[nodiscard]] int entryCount() const;
	[[nodiscard]] QString statusText() const;
	[[nodiscard]] QVector<qsizetype> selectedEntryIndexes() const;
	void refreshPresentation();
	std::function<QList<QUrl>(const QVector<qsizetype>&)> extractForDrag;

signals:
	void stateChanged();
	void entriesReady();
	void entrySelectionChanged();
	// The preview follows the current occurrence, independently of multi-selection.
	void currentEntryChanged();
	void allEntriesSelected(int count);

protected:
	void startDrag(Qt::DropActions supportedActions) override;

private:
	struct Request { PackageArchive archive; QString revision, folder, query; PackageReadControl control; };
	struct Work;
	PackageEntryModel* m_entries = nullptr;
	std::optional<Request> m_request, m_pending;
	std::shared_ptr<Work> m_work;
	std::shared_ptr<const PackageBrowserIndex> m_cache;
	QString m_cacheRevision;
	QThread* m_thread = nullptr;
	QTimer* m_progress = nullptr;
	quint64 m_serial = 0;
	bool m_busy = false, m_retry = false, m_selectAll = false;
	QString m_selectPath;
	qint64 m_selectOrdinal = -1;
	QString m_status;
	void invalidate();
	void startNext();
	void restoreSelection();
};

} // namespace vibestudio
