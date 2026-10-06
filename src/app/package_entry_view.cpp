#include "app/package_entry_view.h"
#include "app/studio_icons.h"
#include "app/studio_theme.h"
#include "core/asset_formats.h"
#include "core/package_preview.h"

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QLocale>
#include <QMimeData>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <new>
#include <limits>

namespace vibestudio {

QString packageEntryIconName(const PackageEntry& entry)
{
	if (entry.kind == PackageEntryKind::Directory) { return QStringLiteral("folder"); }
	const auto hint = entry.typeHint.toLower();
	if (hint.startsWith(QStringLiteral("image/")) || hint.contains(QStringLiteral("texture"))) { return QStringLiteral("image"); }
	if (hint.startsWith(QStringLiteral("audio/"))) { return QStringLiteral("waveform"); }
	if (hint.startsWith(QStringLiteral("model/"))) { return QStringLiteral("cube"); }
	if (hint == QStringLiteral("text/map") || hint.endsWith(QStringLiteral("/bsp"))) { return QStringLiteral("map"); }
	if (hint == QStringLiteral("text/shader")) { return QStringLiteral("layers"); }
	const auto* format = assetFormatForPath(entry.virtualPath);
	if (format && format->readCapability == QStringLiteral("archive")) { return QStringLiteral("package"); }
	switch (assetPreviewKindForEntry(entry.virtualPath, entry.typeHint)) {
	case AssetPreviewKind::Image: return QStringLiteral("image");
	case AssetPreviewKind::Audio: return QStringLiteral("waveform");
	default: return QStringLiteral("file");
	}
}

class PackageEntryModel final : public QAbstractListModel {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioPackageBrowser)
public:
	explicit PackageEntryModel(QObject* parent) : QAbstractListModel(parent) {}
	std::shared_ptr<const PackageBrowserIndex> snapshot;
	QVector<qsizetype> rows;
	QString message, detail;
	bool searching = false;
	std::function<QList<QUrl>(const QVector<qsizetype>&)> extract;

	int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : rows.isEmpty() ? (message.isEmpty() ? 0 : 1) : static_cast<int>(rows.size()); }
	Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		return snapshot && index.isValid() && index.row() >= 0 && index.row() < rows.size()
			? Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled : Qt::NoItemFlags;
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) { return {}; }
		if (rows.isEmpty()) {
			if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) { return message; }
			if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) { return detail; }
			return {};
		}
		const auto at = rows.at(index.row()); const auto& entry = snapshot->entries.at(at);
		const bool folder = entry.kind == PackageEntryKind::Directory;
		switch (role) {
		case Qt::UserRole: return entry.virtualPath;
		case Qt::UserRole + 1: return entry.note.isEmpty() ? QStringLiteral("completed") : QStringLiteral("warning");
		case Qt::UserRole + 3: return folder;
		case Qt::UserRole + 6: return static_cast<qlonglong>(at);
		case Qt::UserRole + 7: return entry.sourceOrdinal;
		case Qt::AccessibleDescriptionRole: return entry.note;
		case Qt::DecorationRole: return studioIcon(packageEntryIconName(entry), folder ? StudioIconTone::Accent : StudioIconTone::Muted);
		case Qt::ForegroundRole: return entry.note.isEmpty() ? QVariant() : QVariant(currentStudioTheme().colors.warning);
		default: break;
		}
		if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::AccessibleTextRole) { return {}; }
		QString occurrence;
		if (snapshot->occurrences.at(at) > 1) { occurrence = entry.sourceOrdinal >= 0 ? tr(" (source entry %1)").arg(entry.sourceOrdinal + 1) : tr(" (planned folder)"); }
		// Qt's formattedDataSize accepts a signed count. Preserve large ZIP64
		// metadata as an exact positive number in display and accessible text.
		const auto size = entry.sizeBytes > static_cast<quint64>(std::numeric_limits<qint64>::max())
			? tr("%1 bytes").arg(QLocale().toString(entry.sizeBytes))
			: QLocale().formattedDataSize(static_cast<qint64>(entry.sizeBytes), 2);
		auto meta = folder ? tr("%n item(s)", nullptr, static_cast<int>(snapshot->childCounts.at(at)))
			: QStringList{size, entry.typeHint, entry.storageMethod.isEmpty() ? tr("unknown") : entry.storageMethod}.join(QStringLiteral("  ·  "));
		if (!folder && !entry.readable) { meta += QStringLiteral("  ·  ") + tr("Unreadable"); }
		if (role == Qt::AccessibleTextRole) { return QStringLiteral("%1, %2, %3").arg(entry.virtualPath + occurrence, folder ? tr("Directory") : tr("File"), meta); }
		const auto name = role == Qt::ToolTipRole || searching ? entry.virtualPath : packageVirtualPathFileName(entry.virtualPath);
		// Keep technical filenames such as 049999.txt in their actual order in
		// RTL layouts, independently of the translated occurrence/metadata text.
		const auto displayedPath = QChar(0x2066) + (name.isEmpty() ? entry.virtualPath : name) + QChar(0x2069);
		const auto displayedOccurrence = occurrence.isEmpty() ? QString() : QLatin1Char(' ') + QChar(0x2068) + occurrence.trimmed() + QChar(0x2069);
		const auto displayedMetadata = QChar(0x2068) + meta + QChar(0x2069);
		const auto label = QStringLiteral("%1%2\n%3").arg(displayedPath, displayedOccurrence, displayedMetadata);
		return role == Qt::ToolTipRole && !entry.note.isEmpty() ? label + QLatin1Char('\n') + entry.note : label;
	}
	void showMessage(const QString& text, const QString& description)
	{
		const bool reset = snapshot || !rows.isEmpty() || message.isEmpty() != text.isEmpty();
		if (reset) { beginResetModel(); }
		snapshot.reset(); rows.clear(); message = text; detail = description;
		if (reset) { endResetModel(); }
		else if (rowCount()) { emit dataChanged(index(0), index(0)); }
	}
	void install(std::shared_ptr<const PackageBrowserIndex> value, QVector<qsizetype> selected, bool search, const QString& empty)
	{
		beginResetModel(); snapshot = std::move(value); rows = std::move(selected); searching = search;
		message = empty; detail.clear(); endResetModel();
	}
	QStringList mimeTypes() const override { return {QStringLiteral("text/uri-list")}; }
	Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }
	QMimeData* mimeData(const QModelIndexList& indexes) const override
	{
		QVector<qsizetype> selected; QSet<qsizetype> seen;
		for (const auto& index : indexes) {
			if (index.model() != this || !flags(index).testFlag(Qt::ItemIsSelectable)) { continue; }
			const auto at = rows.at(index.row()); if (!seen.contains(at)) { selected << at; seen.insert(at); }
		}
		const auto urls = extract && !selected.isEmpty() ? extract(selected) : QList<QUrl>();
		if (urls.isEmpty()) { return nullptr; }
		auto* result = new QMimeData; result->setUrls(urls); return result;
	}
};

struct PackageEntryView::Work {
	quint64 serial = 0;
	std::atomic_bool cancelled = false;
	std::atomic<qint64> records = 0;
	std::shared_ptr<const PackageBrowserIndex> index;
	QVector<qsizetype> rows;
	QString error;
};

PackageEntryView::PackageEntryView(QWidget* parent) : QListView(parent)
{
	m_entries = new PackageEntryModel(this); setModel(m_entries);
	setUniformItemSizes(true); setLayoutMode(QListView::Batched); setBatchSize(256);
	m_entries->extract = [this](const auto& indexes) { return !m_busy && extractForDrag ? extractForDrag(indexes) : QList<QUrl>(); };
	connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { emit entrySelectionChanged(); });
	connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { emit currentEntryChanged(); emit entrySelectionChanged(); });
	// QItemSelectionModel::reset clears current/selection without their usual
	// change signals. Consumers must clear previews and actions while listing.
	connect(m_entries, &QAbstractItemModel::modelReset, this, [this] { emit currentEntryChanged(); emit entrySelectionChanged(); });
	m_progress = new QTimer(this); m_progress->setInterval(75);
	connect(m_progress, &QTimer::timeout, this, [this] {
		if (!m_work || m_work->serial != m_serial || !m_busy) { return; }
		m_status = tr("Preparing package entries… %1 records checked").arg(locale().toString(m_work->records.load()));
		m_entries->showMessage(m_status, m_status); emit stateChanged();
	});
}

PackageEntryView::~PackageEntryView()
{
	invalidate();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

void PackageEntryView::invalidate()
{
	++m_serial; m_pending.reset(); m_progress->stop(); m_selectAll = false;
	if (m_work) { m_work->cancelled = true; }
}

void PackageEntryView::showMessage(const QString& message, const QString& detail)
{
	invalidate(); m_request.reset(); m_cache.reset(); m_cacheRevision.clear(); m_busy = false; m_retry = false;
	m_selectPath.clear(); m_selectOrdinal = -1; m_status = message;
	m_entries->showMessage(message, detail); emit stateChanged();
}

void PackageEntryView::showEntries(const PackageArchive& archive, const QString& revision, const QString& folder, const QString& query, const PackageReadControl& control)
{
	if (m_request && m_request->revision == revision && m_request->folder == folder && m_request->query == query && !m_retry) { return; }
	if (currentIndex().isValid() && currentIndex().data(Qt::UserRole).isValid()) {
		m_selectPath = currentIndex().data(Qt::UserRole).toString(); m_selectOrdinal = currentIndex().data(Qt::UserRole + 7).toLongLong();
	}
	invalidate(); m_request = Request{archive, revision, folder, query, control}; m_pending = m_request;
	if (m_cacheRevision != revision) { m_cache.reset(); m_cacheRevision.clear(); }
	m_busy = true; m_retry = false; m_status = tr("Preparing package entries…");
	m_entries->showMessage(m_status, m_status); emit stateChanged();
	QTimer::singleShot(0, this, [this] { startNext(); });
}

void PackageEntryView::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto request = std::move(*m_pending); m_pending.reset();
	auto work = std::make_shared<Work>(); work->serial = m_serial; work->index = m_cache; m_work = work;
	m_thread = QThread::create([work, request] {
		try {
			PackageReadControl control;
			control.isCancelled = [work, previous = request.control] {
				if (!work->cancelled && previous.isCancelled && previous.isCancelled()) { work->cancelled = true; }
				return work->cancelled.load();
			};
			control.progress = [work, previous = request.control](const QString& phase, qint64 count, qint64 total) {
				work->records = count; if (previous.progress) { previous.progress(phase, count, total); }
			};
			if (!work->index) { work->index = preparePackageBrowserIndex(request.archive, &work->error, control); }
			if (work->index && !work->cancelled) { filterPackageBrowser(*work->index, request.folder, request.query, &work->rows, &work->error, control); }
		} catch (const std::bad_alloc&) { work->index.reset(); work->rows.clear(); work->error = tr("Not enough memory to prepare package entries."); }
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, work, request] {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; m_progress->stop();
		if (m_work == work) { m_work.reset(); }
		if (work->serial == m_serial && work->cancelled) { cancel(); }
		if (work->serial == m_serial && !work->cancelled) {
			m_busy = false; m_retry = !work->index || !work->error.isEmpty();
			if (m_retry) {
				m_status = work->error.isEmpty() ? tr("Package entries unavailable") : work->error;
				m_entries->showMessage(m_status, m_status);
			} else {
				m_cache = work->index; m_cacheRevision = request.revision;
				const bool search = !request.query.trimmed().isEmpty();
				m_entries->install(work->index, std::move(work->rows), search, search ? tr("No entries match “%1”").arg(request.query.trimmed()) : tr("This folder is empty"));
				m_status = tr("%n item(s)", nullptr, entryCount()); restoreSelection();
			}
			emit stateChanged(); emit entriesReady();
		}
		startNext();
	});
	m_progress->start(); m_thread->start();
}

void PackageEntryView::cancel()
{
	if (!m_busy) { return; }
	invalidate(); m_busy = false; m_retry = true; m_status = tr("Package list cancelled.");
	m_entries->showMessage(m_status, m_status); emit stateChanged();
}

void PackageEntryView::retry()
{
	if (m_retry && m_request) { const auto request = *m_request; showEntries(request.archive, request.revision, request.folder, request.query, request.control); }
}

bool PackageEntryView::readyFor(const QString& revision) const { return !m_busy && !m_retry && m_request && m_request->revision == revision && m_entries->snapshot; }
std::shared_ptr<const PackageBrowserIndex> PackageEntryView::browserIndex(const QString& revision) const
{
	return m_cacheRevision == revision ? m_cache : nullptr;
}

int PackageEntryView::entryCount() const { return static_cast<int>(m_entries->rows.size()); }
QString PackageEntryView::statusText() const { return m_status; }

void PackageEntryView::selectEntry(const QString& path, qint64 ordinal)
{
	m_selectPath = path; m_selectOrdinal = ordinal;
	if (!m_busy) { restoreSelection(); }
}

void PackageEntryView::selectAllWhenReady()
{
	m_selectAll = true; if (!m_busy) { restoreSelection(); }
}

void PackageEntryView::restoreSelection()
{
	if (!entryCount()) { if (m_selectAll) { m_selectAll = false; emit allEntriesSelected(0); } return; }
	int selected = 0;
	for (int row = 0; row < entryCount(); ++row) {
		const auto& entry = m_entries->snapshot->entries.at(m_entries->rows.at(row));
		if (m_selectOrdinal >= 0 ? entry.sourceOrdinal == m_selectOrdinal : entry.virtualPath == m_selectPath) { selected = row; break; }
	}
	setCurrentIndex(model()->index(selected, 0));
	if (m_selectAll) { selectAll(); m_selectAll = false; emit allEntriesSelected(entryCount()); }
	scrollTo(currentIndex());
}

QVector<qsizetype> PackageEntryView::selectedEntryIndexes() const
{
	QVector<qsizetype> result;
	if (m_busy || m_retry || !m_entries->snapshot) { return result; }
	for (const auto& row : selectionModel()->selectedRows()) { result << row.data(Qt::UserRole + 6).toLongLong(); }
	if (result.isEmpty() && currentIndex().data(Qt::UserRole + 6).isValid()) { result << currentIndex().data(Qt::UserRole + 6).toLongLong(); }
	return result;
}

void PackageEntryView::refreshPresentation() { viewport()->update(); }
void PackageEntryView::startDrag(Qt::DropActions supportedActions) { QListView::startDrag(supportedActions & Qt::CopyAction); }

} // namespace vibestudio
