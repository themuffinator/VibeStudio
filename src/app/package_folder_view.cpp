#include "app/package_folder_view.h"
#include "app/studio_icons.h"
#include "app/studio_theme.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QHeaderView>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <algorithm>

namespace vibestudio {

class PackageFolderModel final : public QAbstractItemModel {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioPackageFolders)
public:
	explicit PackageFolderModel(QObject* parent) : QAbstractItemModel(parent) {}
	std::shared_ptr<const PackageBrowserIndex> snapshot;
	QString rootLabel, rootDetail, message, detail;
	bool rootWarning = false;

	QModelIndex folder(qsizetype at) const
	{
		if (!snapshot || at < 0 || at >= snapshot->folders.size()) { return {}; }
		return createIndex(static_cast<int>(snapshot->folders.at(at).row), 0, static_cast<quintptr>(at));
	}
	int columnCount(const QModelIndex& = {}) const override { return 1; }
	int rowCount(const QModelIndex& parent = {}) const override
	{
		if (parent.isValid() && (parent.model() != this || parent.column() != 0)) { return 0; }
		if (!parent.isValid()) { return snapshot ? 1 : message.isEmpty() ? 0 : 1; }
		if (!snapshot || parent.internalId() >= static_cast<quintptr>(snapshot->folders.size())) { return 0; }
		return static_cast<int>(snapshot->folders.at(static_cast<qsizetype>(parent.internalId())).children.size());
	}
	QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override
	{
		if (column != 0 || row < 0 || row >= rowCount(parent)) { return {}; }
		if (!parent.isValid()) { return createIndex(0, 0, static_cast<quintptr>(0)); }
		return folder(snapshot->folders.at(static_cast<qsizetype>(parent.internalId())).children.at(row));
	}
	QModelIndex parent(const QModelIndex& index) const override
	{
		if (!snapshot || !index.isValid() || index.model() != this || index.internalId() >= static_cast<quintptr>(snapshot->folders.size())) { return {}; }
		return folder(snapshot->folders.at(static_cast<qsizetype>(index.internalId())).parent);
	}
	Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		if (!index.isValid() || index.model() != this) { return Qt::NoItemFlags; }
		return snapshot ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::ItemIsEnabled;
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid() || index.model() != this) { return {}; }
		if (!snapshot) {
			if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) { return message; }
			if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) { return detail; }
			return {};
		}
		const auto at = static_cast<qsizetype>(index.internalId());
		if (at < 0 || at >= snapshot->folders.size()) { return {}; }
		const auto& node = snapshot->folders.at(at);
		const bool root = at == 0;
		const auto note = node.entry >= 0 ? snapshot->entries.at(node.entry).note : QString();
		const bool warning = root ? rootWarning : !note.isEmpty();
		switch (role) {
		case Qt::DisplayRole:
			return root ? QChar(0x2068) + rootLabel + QChar(0x2069)
				: QChar(0x2066) + packageVirtualPathFileName(node.path) + QChar(0x2069);
		case Qt::DecorationRole: return studioIcon(root ? QStringLiteral("package") : QStringLiteral("folder"), StudioIconTone::Muted);
		case Qt::ForegroundRole: return warning ? QVariant(currentStudioTheme().colors.warning) : QVariant();
		case Qt::ToolTipRole: return root ? rootDetail : note.isEmpty() ? node.path : node.path + QLatin1Char('\n') + note;
		case Qt::AccessibleTextRole: return root ? tr("Package root: %1").arg(rootLabel) : tr("Folder %1").arg(node.path);
		case Qt::AccessibleDescriptionRole: return root ? rootDetail : note;
		case Qt::UserRole: return node.path;
		case Qt::UserRole + 1: return warning ? QStringLiteral("warning") : QStringLiteral("completed");
		case Qt::UserRole + 3: return true;
		case Qt::UserRole + 6: return static_cast<qlonglong>(node.entry);
		default: return {};
		}
	}
	void showMessage(const QString& text, const QString& description)
	{
		const bool reset = snapshot || message.isEmpty() != text.isEmpty();
		if (reset) { beginResetModel(); }
		snapshot.reset(); message = text; detail = description;
		if (reset) { endResetModel(); }
		else if (rowCount()) { emit dataChanged(index(0, 0), index(0, 0)); }
	}
	void install(std::shared_ptr<const PackageBrowserIndex> value, const QString& label, const QString& description, bool warning)
	{
		const bool reset = snapshot != value;
		if (!reset && rootLabel == label && rootDetail == description && rootWarning == warning) { return; }
		if (reset) { beginResetModel(); }
		snapshot = std::move(value); rootLabel = label; rootDetail = description; rootWarning = warning;
		message.clear(); detail.clear();
		if (reset) { endResetModel(); }
		else { emit dataChanged(index(0, 0), index(0, 0)); }
	}
};

PackageFolderView::PackageFolderView(QWidget* parent) : QTreeView(parent)
{
	m_folders = new PackageFolderModel(this); setModel(m_folders);
	setHeaderHidden(true); setUniformRowHeights(true); setSelectionMode(QAbstractItemView::SingleSelection);
	setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	header()->setStretchLastSection(false); header()->setSectionResizeMode(QHeaderView::Fixed);
	setAccessibleName(tr("Package folders"));
	connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
		updateColumnWidth(index); emit folderSelectionChanged();
	});
	connect(this, &QTreeView::expanded, this, [this](const QModelIndex& index) { updateColumnWidth(index, true); });
	connect(m_folders, &QAbstractItemModel::modelReset, this, &PackageFolderView::folderSelectionChanged);
}

void PackageFolderView::showFolders(std::shared_ptr<const PackageBrowserIndex> index, const QString& revision,
	const QString& rootLabel, const QString& rootDetail, bool rootWarning)
{
	if (!index || index->folders.isEmpty()) { showMessage(tr("Package folders unavailable")); return; }
	const bool reset = m_folders->snapshot != index;
	if (reset) { m_seenDepth = 0; m_widestCaption.clear(); }
	m_revision = revision; m_folders->install(std::move(index), rootLabel, rootDetail, rootWarning);
	updateColumnWidth(m_folders->index(0, 0));
	if (reset) { expand(m_folders->index(0, 0)); }
}

void PackageFolderView::showMessage(const QString& message, const QString& detail)
{
	m_revision.clear(); m_seenDepth = 0; m_widestCaption.clear();
	m_folders->showMessage(message, detail); updateColumnWidth(m_folders->index(0, 0));
}

bool PackageFolderView::readyFor(const QString& revision) const { return m_folders->snapshot && m_revision == revision; }

QModelIndex PackageFolderView::folderIndex(const QString& path) const
{
	if (!m_folders->snapshot) { return {}; }
	return m_folders->folder(m_folders->snapshot->folderLookup.value(path, -1));
}

QString PackageFolderView::selectedFolder() const
{
	return m_folders->snapshot ? currentIndex().data(Qt::UserRole).toString() : QString();
}

bool PackageFolderView::selectFolder(const QString& path)
{
	const auto index = folderIndex(path);
	if (!index.isValid()) { return false; }
	for (auto ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent()) { expand(ancestor); }
	updateColumnWidth(index); setCurrentIndex(index); scrollTo(index, QAbstractItemView::PositionAtCenter); return true;
}

void PackageFolderView::scrollTo(const QModelIndex& index, ScrollHint hint)
{
	updateColumnWidth(index); QTreeView::scrollTo(index, hint);
	if (!index.isValid() || index.model() != model()) { return; }
	auto rectangle = visualRect(index);
	if (!rectangle.isValid()) { return; }
	const auto icon = iconSize().width() > 0 ? iconSize().width() : style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
	const auto padding = 12 + 2 * style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this);
	const auto captionWidth = fontMetrics().horizontalAdvance(index.data(Qt::DisplayRole).toString()) + icon + padding;
	const auto width = std::min({rectangle.width(), captionWidth, viewport()->width()});
	if (isRightToLeft()) { rectangle.setLeft(rectangle.right() - width + 1); }
	else { rectangle.setWidth(width); }
	// Qt can keep the wide column visible while the indented caption is outside
	// the viewport. Scroll the selected caption into view in either direction.
	const auto delta = rectangle.left() < 0 ? rectangle.left()
		: rectangle.right() >= viewport()->width() ? rectangle.right() - viewport()->width() + 1 : 0;
	horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (isRightToLeft() ? -delta : delta));
}

void PackageFolderView::updateColumnWidth(const QModelIndex& index, bool children)
{
	if (!m_folders) { return; }
	if (index.isValid()) {
		int depth = children ? 1 : 0;
		for (auto ancestor = index.parent(); ancestor.isValid(); ancestor = ancestor.parent()) { ++depth; }
		m_seenDepth = std::max(m_seenDepth, depth);
		const auto caption = index.data(Qt::DisplayRole).toString();
		if (fontMetrics().horizontalAdvance(caption) > fontMetrics().horizontalAdvance(m_widestCaption)) { m_widestCaption = caption; }
	}
	// Measure only visited captions and depth, never every row in a wide tree.
	// The selected caption has room even when indentation exceeds the viewport.
	const auto icon = iconSize().width() > 0 ? iconSize().width() : style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
	const auto padding = 12 + 2 * style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this);
	const auto width = (m_seenDepth + 1) * indentation() + fontMetrics().horizontalAdvance(m_widestCaption) + icon + padding;
	header()->resizeSection(0, std::max(viewport()->width(), width));
}

void PackageFolderView::resizeEvent(QResizeEvent* event)
{
	QTreeView::resizeEvent(event); updateColumnWidth(currentIndex());
}

void PackageFolderView::changeEvent(QEvent* event)
{
	QTreeView::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::StyleChange) {
		updateColumnWidth(currentIndex());
	}
}

void PackageFolderView::refreshPresentation() { updateColumnWidth(currentIndex()); viewport()->update(); }

} // namespace vibestudio
