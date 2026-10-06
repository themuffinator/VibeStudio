#include "app/package_extraction_paths.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {
namespace {

QString pathKey(const QString& path)
{
	return normalizePackageVirtualPath(path, false).normalizedPath.normalized(QString::NormalizationForm_C).toCaseFolded();
}

class OutputPathDelegate final : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		auto size = QStyledItemDelegate::sizeHint(option, index);
		const auto* tree = qobject_cast<const QTreeWidget*>(parent());
		if (tree) {
			const QRect bounds = option.fontMetrics.boundingRect(QRect(0, 0, qMax(1, tree->columnWidth(index.column()) - 16), 1000000),
				Qt::TextWordWrap, index.data(Qt::DisplayRole).toString());
			size.setHeight(qMax(size.height(), bounds.height() + 12));
		}
		return size;
	}
	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		if (index.column() != 1) { return nullptr; }
		QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
		if (editor) {
			editor->setLayoutDirection(Qt::LeftToRight);
			editor->setAccessibleName(QCoreApplication::translate("VibeStudioPackagePaths", "Relative extraction path"));
		}
		return editor;
	}
};

class ExtractionPathsDialog final : public QDialog {
public:
	ExtractionPathsDialog(const QVector<PackageEntry>& entries, const QVector<qsizetype>& selected,
		const QHash<qsizetype, QString>& overrides, const QSet<QString>& needsMapping, QWidget* parent)
		: QDialog(parent)
	{
		setObjectName(QStringLiteral("packageExtractionPaths"));
		setWindowTitle(QCoreApplication::translate("VibeStudioPackagePaths", "Extraction Paths"));
		setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal);
		resize(900, 540);
		auto* layout = new QVBoxLayout(this);
		m_status = new QLabel;
		m_status->setObjectName(QStringLiteral("extractionPathStatus"));
		m_status->setAccessibleName(QCoreApplication::translate("VibeStudioPackagePaths", "Extraction path validation"));
		m_status->setWordWrap(true);
		layout->addWidget(m_status);
		m_paths = new QTreeWidget;
		m_paths->setObjectName(QStringLiteral("extractionPaths"));
		m_paths->setAccessibleName(QCoreApplication::translate("VibeStudioPackagePaths", "Source entries and output paths"));
		m_paths->setAccessibleDescription(QCoreApplication::translate("VibeStudioPackagePaths", "Each source occurrence keeps its own row. Output paths are editable and relative to the chosen directory."));
		m_paths->setRootIsDecorated(false);
		m_paths->setWordWrap(true);
		m_paths->setTextElideMode(Qt::ElideNone);
		connect(m_paths->header(), &QHeaderView::sectionResized, m_paths, [this]() { m_paths->doItemsLayout(); });
		m_paths->setColumnCount(2);
		m_paths->setHeaderLabels({QCoreApplication::translate("VibeStudioPackagePaths", "Source entry"), QCoreApplication::translate("VibeStudioPackagePaths", "Output path")});
		m_paths->setItemDelegate(new OutputPathDelegate(m_paths));
		m_paths->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
		m_paths->header()->setSectionResizeMode(QHeaderView::Stretch);
		for (const qsizetype index : selected) {
			const auto& entry = entries.at(index);
			if (entry.kind == PackageEntryKind::Directory) {
				for (QString path = pathKey(entry.virtualPath); !path.isEmpty(); path = packageVirtualPathParent(path)) { m_directories.insert(path); }
				continue;
			}
			const qint64 ordinal = entry.sourceOrdinal >= 0 ? entry.sourceOrdinal : index;
			const QString label = QCoreApplication::translate("VibeStudioPackagePaths", "%1 (source entry %2)").arg(entry.virtualPath).arg(ordinal + 1);
			QString output = overrides.value(index, entry.virtualPath);
			if (needsMapping.contains(pathKey(output))) { output = QStringLiteral("source-entry-%1/%2").arg(ordinal + 1).arg(output); }
			auto* row = new QTreeWidgetItem(m_paths, {label, output});
			row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
			row->setData(0, Qt::UserRole, static_cast<qlonglong>(index));
			row->setData(0, Qt::AccessibleTextRole, label);
			row->setToolTip(0, label);
			row->setTextAlignment(1, Qt::AlignLeft | Qt::AlignVCenter);
		}
		layout->addWidget(m_paths, 1);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
		m_extract = buttons->addButton(QCoreApplication::translate("VibeStudioPackagePaths", "Extract"), QDialogButtonBox::AcceptRole);
		m_extract->setObjectName(QStringLiteral("extractMappedEntries"));
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addWidget(buttons);
		connect(m_paths, &QTreeWidget::itemChanged, this, [this]() { validatePaths(); });
		if (m_paths->topLevelItemCount()) { m_paths->setCurrentItem(m_paths->topLevelItem(0), 1); }
		validatePaths();
	}

	QVector<PackageExtractionSelection> selection() const
	{
		QVector<PackageExtractionSelection> result;
		for (int index = 0; index < m_paths->topLevelItemCount(); ++index) {
			const auto* row = m_paths->topLevelItem(index);
			result.append({static_cast<qsizetype>(row->data(0, Qt::UserRole).toLongLong()), row->text(1)});
		}
		return result;
	}

private:
	void validatePaths()
	{
		const QSignalBlocker blocker(m_paths);
		QHash<QString, int> counts;
		const auto rows = selection();
		for (const auto& row : rows) { ++counts[pathKey(row.outputVirtualPath)]; }
		QString firstError;
		for (qsizetype index = 0; index < rows.size(); ++index) {
			const auto& path = rows.at(index).outputVirtualPath;
			const auto normalized = normalizePackageVirtualPath(path, false);
			const QString key = pathKey(path);
			QString error;
			if (!normalized.isSafe() || packageFilesystemPathIssue(normalized.normalizedPath) != PackagePathIssue::None) {
				error = QCoreApplication::translate("VibeStudioPackagePaths", "Enter a safe relative file path.");
			} else if (counts.value(key) > 1 || m_directories.contains(key)) {
				error = QCoreApplication::translate("VibeStudioPackagePaths", "This output path is already used by another entry.");
			}
			for (QString parent = packageVirtualPathParent(key); error.isEmpty() && !parent.isEmpty(); parent = packageVirtualPathParent(parent)) {
				if (counts.contains(parent)) { error = QCoreApplication::translate("VibeStudioPackagePaths", "Another file occupies a required output directory."); }
			}
			auto* item = m_paths->topLevelItem(static_cast<int>(index));
			item->setToolTip(1, error.isEmpty() ? path : error);
			item->setData(1, Qt::AccessibleTextRole, error.isEmpty() ? path : path + QStringLiteral(": ") + error);
			if (firstError.isEmpty() && !error.isEmpty()) { firstError = error; }
		}
		m_extract->setEnabled(!rows.isEmpty() && firstError.isEmpty());
		m_status->setText(firstError.isEmpty()
			? QCoreApplication::translate("VibeStudioPackagePaths", "%n distinct output path(s).", nullptr, static_cast<int>(rows.size()))
			: firstError);
	}
	QTreeWidget* m_paths = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_extract = nullptr;
	QSet<QString> m_directories;
};

} // namespace

bool reviewPackageExtractionPaths(QWidget* parent, const PackageArchiveReader& source, PackageExtractionRequest* request)
{
	if (!request) { return false; }
	const auto entries = source.entries();
	QSet<qsizetype> selected;
	QHash<qsizetype, QString> overrides;
	for (const auto& entry : request->entrySelections) {
		if (entry.entryIndex < 0 || entry.entryIndex >= entries.size()) { return true; } // Let the shared preflight report it.
		selected.insert(entry.entryIndex);
		if (!entry.outputVirtualPath.isEmpty()) { overrides.insert(entry.entryIndex, entry.outputVirtualPath); }
	}
	for (qsizetype index = 0; index < entries.size(); ++index) {
		if (request->extractAll) { selected.insert(index); continue; }
		const QString key = pathKey(entries.at(index).virtualPath);
		for (const auto& path : request->virtualPaths) {
			const QString prefix = pathKey(path);
			if (key == prefix || key.startsWith(prefix + QLatin1Char('/'))) { selected.insert(index); break; }
		}
	}
	QHash<QString, int> counts;
	QSet<QString> files;
	for (const auto index : selected) {
		const auto& entry = entries.at(index);
		const QString key = pathKey(overrides.value(index, entry.virtualPath));
		++counts[key];
		if (entry.kind == PackageEntryKind::File) { files.insert(key); }
	}
	QSet<QString> conflicts;
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
		if (it.value() > 1) { conflicts.insert(it.key()); }
		for (QString parent = packageVirtualPathParent(it.key()); !parent.isEmpty(); parent = packageVirtualPathParent(parent)) {
			if (files.contains(parent)) { conflicts.insert(parent); }
		}
	}
	if (conflicts.isEmpty()) { return true; }
	QVector<qsizetype> ordered(selected.begin(), selected.end());
	std::sort(ordered.begin(), ordered.end());
	ExtractionPathsDialog dialog(entries, ordered, overrides, conflicts, parent);
	if (dialog.exec() != QDialog::Accepted) { return false; }
	if (request->extractAll) {
		request->virtualPaths.clear();
		for (const auto index : ordered) {
			if (entries.at(index).kind == PackageEntryKind::Directory) { request->virtualPaths << entries.at(index).virtualPath; }
		}
	}
	request->extractAll = false;
	request->entrySelections = dialog.selection();
	return true;
}

} // namespace vibestudio
