#include "app/package_staging_view.h"
#include "app/studio_theme.h"

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QStyledItemDelegate>

namespace vibestudio {
namespace {
constexpr int CompactRole = Qt::UserRole + 10;
constexpr int DetailsRole = Qt::UserRole + 11;

QString displayLines(const QString& text)
{
	QStringList lines = text.split(QLatin1Char('\n'));
	for (auto& line : lines) { line = QChar(0x2068) + line + QChar(0x2069); }
	return lines.join(QLatin1Char('\n'));
}

QString operationName(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add: return PackageStagingView::tr("Add");
	case PackageStageOperationType::Replace: return PackageStagingView::tr("Replace");
	case PackageStageOperationType::Rename: return PackageStagingView::tr("Rename");
	case PackageStageOperationType::Delete: return PackageStagingView::tr("Delete");
	case PackageStageOperationType::CreateDirectory: return PackageStagingView::tr("Create Directory");
	case PackageStageOperationType::RenameDirectory: return PackageStagingView::tr("Rename Directory");
	case PackageStageOperationType::DeleteDirectory: return PackageStagingView::tr("Delete Directory");
	}
	return PackageStagingView::tr("Stage");
}

class StagingDelegate final : public QStyledItemDelegate {
public:
	explicit StagingDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		// Layout may visit every row, but must not translate/format every record.
		// Only the small fixed overview/composition has variable wrapped height.
		if (index.data(CompactRole).toBool()) { return {1, option.fontMetrics.lineSpacing() * 2 + 10}; }
		return QStyledItemDelegate::sizeHint(option, index);
	}
protected:
	void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
	{
		QStyledItemDelegate::initStyleOption(option, index);
		if (index.data(CompactRole).toBool()) {
			option->features &= ~QStyleOptionViewItem::WrapText;
			option->textElideMode = Qt::ElideRight;
		}
	}
};
}

class PackageStagingListModel final : public QAbstractListModel {
	Q_DECLARE_TR_FUNCTIONS(PackageStagingView)
public:
	explicit PackageStagingListModel(QObject* parent) : QAbstractListModel(parent) {}
	QVector<PackageStagingRow> before, after;
	QVector<PackageStageOperation> operations;
	QVector<PackageStageConflict> conflicts;
	int rowCount(const QModelIndex& parent = {}) const override
	{
		return parent.isValid() ? 0 : static_cast<int>(before.size() + operations.size() + conflicts.size() + after.size());
	}
	const PackageStagingRow* overview(int row) const
	{
		if (row < before.size()) { return &before.at(row); }
		const auto tail = row - before.size() - operations.size() - conflicts.size();
		return tail >= 0 && tail < after.size() ? &after.at(tail) : nullptr;
	}
	Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		if (index.model() != this || !index.isValid() || index.row() >= rowCount()) { return Qt::NoItemFlags; }
		const auto* row = overview(index.row());
		return row && !row->enabled ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (index.model() != this || !index.isValid() || index.row() >= rowCount()) { return {}; }
		const auto* row = overview(index.row());
		const auto offset = index.row() - before.size();
		const auto* operation = !row && offset < operations.size() ? &operations.at(offset) : nullptr;
		const auto* conflict = !row && !operation ? &conflicts.at(offset - operations.size()) : nullptr;
		if (role == CompactRole) { return !row; }
		if (role == Qt::UserRole + 5) { return operation ? operation->id : QVariant(); }
		if (role == Qt::UserRole + 7) { return operation ? operation->sourceOrdinal : QVariant(); }
		if (role == Qt::UserRole) {
			if (conflict) { return conflict->virtualPath; }
			if (!operation) { return {}; }
			return operation->type == PackageStageOperationType::Rename || operation->type == PackageStageOperationType::RenameDirectory
				? operation->targetVirtualPath : operation->virtualPath;
		}
		const auto state = row ? row->state : operation ? QStringLiteral("running") : conflict->blocking ? QStringLiteral("failed") : QStringLiteral("warning");
		if (role == Qt::UserRole + 1) { return state; }
		if (role == Qt::ForegroundRole) {
			const auto color = studioThemeStateColor(currentStudioTheme(), state);
			return color.isValid() ? QVariant(color) : QVariant();
		}
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::AccessibleDescriptionRole
			&& role != Qt::ToolTipRole && role != DetailsRole) { return {}; }
		if (row) { return role == Qt::DisplayRole ? displayLines(row->text) : row->text; }
		const bool display = role == Qt::DisplayRole;
		const auto path = [display](const QString& text) { return display ? QChar(0x2066) + text + QChar(0x2069) : text; };
		if (conflict) {
			const auto text = QStringLiteral("%1\n%2").arg(conflict->blocking ? tr("Blocked") : tr("Notice"),
				conflict->virtualPath.isEmpty() ? conflict->message : QStringLiteral("%1: %2").arg(path(conflict->virtualPath), conflict->message));
			return display ? displayLines(text) : text;
		}
		const auto label = operation->sourceOrdinal >= 0 ? tr("%1 (source entry %2)").arg(path(operation->virtualPath)).arg(operation->sourceOrdinal + 1) : path(operation->virtualPath);
		QStringList lines{QStringLiteral("%1: %2").arg(operationName(operation->type), label)};
		const auto policy = tr("Conflict policy: %1").arg(packageStageConflictResolutionId(operation->conflictResolution));
		if (display) {
			lines << (operation->targetVirtualPath.isEmpty() ? policy : tr("Target: %1").arg(path(operation->targetVirtualPath)));
		} else {
			if (operation->hasInlineBytes) { lines << tr("Generated asset: %1; content is held in the staging plan.").arg(QLocale().formattedDataSize(operation->inlineBytes.size())); }
			if (!operation->targetVirtualPath.isEmpty()) { lines << tr("Target: %1").arg(operation->targetVirtualPath); }
			if (!operation->sourceFilePath.isEmpty()) { lines << tr("Source: %1").arg(QDir::toNativeSeparators(operation->sourceFilePath)); }
			lines << policy;
			if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) { lines << tr("Delete, or Unstage in the context menu, takes this change out of the plan. Enter shows the entry."); }
		}
		const auto text = lines.join(QLatin1Char('\n'));
		return display ? displayLines(text) : text;
	}
	void install(QVector<PackageStagingRow> prefix, QVector<PackageStageOperation> edits,
		QVector<PackageStageConflict> issues, QVector<PackageStagingRow> suffix)
	{
		if (before == prefix && after == suffix && operations.constData() == edits.constData() && operations.size() == edits.size()
			&& conflicts.constData() == issues.constData() && conflicts.size() == issues.size()) { return; }
		beginResetModel(); before = std::move(prefix); after = std::move(suffix);
		operations = std::move(edits); conflicts = std::move(issues); endResetModel();
	}
};

PackageStagingView::PackageStagingView(QWidget* parent) : QListView(parent)
{
	m_rows = new PackageStagingListModel(this); setModel(m_rows); setItemDelegate(new StagingDelegate(this));
	setWordWrap(true); setResizeMode(QListView::Adjust); setTextElideMode(Qt::ElideNone);
	setLayoutMode(QListView::Batched); setBatchSize(256); setSelectionMode(QAbstractItemView::ExtendedSelection);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { emit detailsChanged(currentDetails()); });
	connect(m_rows, &QAbstractItemModel::modelReset, this, [this] { emit detailsChanged({}); });
}

void PackageStagingView::setContents(QVector<PackageStagingRow> before, QVector<PackageStageOperation> operations,
	QVector<PackageStageConflict> conflicts, QVector<PackageStagingRow> after)
{
	m_rows->install(std::move(before), std::move(operations), std::move(conflicts), std::move(after));
}

void PackageStagingView::showMessage(const QString& message)
{
	setContents({{message, QStringLiteral("idle"), false}}, {}, {}, {});
}

void PackageStagingView::refreshPresentation()
{
	// Fonts/layout are handled by Qt; only visible state colours need repainting.
	viewport()->update();
}

QStringList PackageStagingView::selectedOperationIds() const
{
	QStringList result;
	for (const auto& index : selectionModel()->selectedRows()) {
		const auto id = index.data(Qt::UserRole + 5).toString(); if (!id.isEmpty()) { result << id; }
	}
	return result;
}

QString PackageStagingView::currentDetails() const
{
	return currentIndex().isValid() ? currentIndex().data(DetailsRole).toString() : QString();
}

} // namespace vibestudio
