#include "app/code_files_panel.h"
#include "app/code_files_worker.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace vibestudio {
namespace {
QString pathKey(const QString& path)
{
	const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
	return cleaned.toCaseFolded();
#else
	return cleaned;
#endif
}
} // namespace

CodeFilesPanel::CodeFilesPanel(QTreeWidget* tree, QLineEdit* filter, QWidget* parent)
	: QWidget(parent), m_tree(tree), m_filter(filter)
{
	setObjectName(QStringLiteral("codeFilesPanel"));
	setAccessibleName(tr("Project files"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_filter);
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("codeFilesStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Project file scan status"));
	layout->addWidget(m_status);
	auto* controls = new QHBoxLayout;
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Scanning project files"));
	m_progress->setRange(0, 0);
	m_progress->setTextVisible(false);
	m_progress->hide();
	controls->addWidget(m_progress, 1);
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setObjectName(QStringLiteral("codeFilesCancel"));
	m_cancel->setAccessibleName(tr("Cancel project file scan"));
	m_cancel->setEnabled(false);
	// Shown beside the progress bar only while a scan runs.
	m_cancel->hide();
	connect(m_cancel, &QPushButton::clicked, this, &CodeFilesPanel::cancel);
	controls->addWidget(m_cancel);
	layout->addLayout(controls);
	m_details = new QLabel;
	m_details->setObjectName(QStringLiteral("codeFilesWarnings"));
	m_details->setTextFormat(Qt::PlainText);
	m_details->setWordWrap(true);
	m_details->setAccessibleName(tr("Project file scan warnings"));
	m_details->hide();
	layout->addWidget(m_details);
	m_queryStatus = new QLabel;
	m_queryStatus->setObjectName(QStringLiteral("codeFilesFilterStatus"));
	m_queryStatus->setTextFormat(Qt::PlainText);
	m_queryStatus->setWordWrap(true);
	m_queryStatus->setAccessibleName(tr("Project file filter status"));
	m_queryStatus->hide();
	layout->addWidget(m_queryStatus);
	layout->addWidget(m_tree, 1);
	m_tree->setUniformRowHeights(true);
	m_rowsTimer = new QTimer(this);
	m_rowsTimer->setSingleShot(true);
	connect(m_rowsTimer, &QTimer::timeout, this, &CodeFilesPanel::appendRows);
	m_worker = new CodeFilesWorker(this);
	m_worker->progress = [this](int files, int entries) {
		setStatus(tr("Scanning: %1 file(s), %2 entries checked…").arg(files).arg(entries));
		if (operationProgress) { operationProgress(files, entries); }
	};
	m_worker->completed = [this](const CodeFilesResult& result) { applyResult(result); };
	connect(m_filter, &QLineEdit::textChanged, this, &CodeFilesPanel::filterFiles);
	setStatus(tr("Open a project to list its files."));
}

void CodeFilesPanel::setStatus(const QString& text)
{
	m_status->setText(text);
	m_status->setAccessibleDescription(text);
}

void CodeFilesPanel::placeholder(const QString& text)
{
	auto* item = new QTreeWidgetItem(m_tree, {text});
	item->setToolTip(0, text);
	item->setFlags(Qt::NoItemFlags);
}

void CodeFilesPanel::retire()
{
	m_rowsTimer->stop();
	m_worker->reset();
	if (m_busy) {
		CodeFilesResult retired;
		retired.rootPath = m_root;
		retired.complete = false;
		retired.cancelled = true;
		retired.state = OperationState::Cancelled;
		m_busy = false;
		if (operationFinished) { operationFinished(retired); }
	}
}

void CodeFilesPanel::setRootPath(const QString& root, bool force)
{
	const QString path = root.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(root).absoluteFilePath());
	if (pathKey(path) != pathKey(m_root)) {
		retire();
		m_root = path;
		m_requested = false;
		m_result = {};
		m_expanded.clear();
		m_directories.clear();
		m_properties.clear();
		m_tree->clear();
		m_selectedPath.clear();
	}
	if (m_root.isEmpty()) {
		m_tree->clear();
		placeholder(tr("Open a project to list its files."));
		setStatus(tr("No project open."));
		m_progress->hide();
		m_cancel->setEnabled(false);
		m_cancel->hide();
		m_details->hide();
		return;
	}
	if (force || !m_requested) { refresh(); }
}

void CodeFilesPanel::refresh()
{
	if (m_root.isEmpty()) { return; }
	retire();
	m_requested = true;
	m_busy = true;
	m_progress->setRange(0, 0);
	m_progress->show();
	m_cancel->setEnabled(true);
	m_cancel->show();
	m_details->hide();
	setStatus(tr("Scanning project files…"));
	if (operationStarted) { operationStarted(); }
	CodeFilesRequest request;
	request.rootPath = m_root;
	m_worker->start(std::move(request));
}

void CodeFilesPanel::cancel()
{
	if (!m_busy) { return; }
	m_cancel->setEnabled(false);
	setStatus(tr("Cancelling project file scan…"));
	if (m_rowsTimer->isActive()) {
		m_rowsTimer->stop();
		m_result.files.resize(m_nextFile);
		m_result.complete = false;
		m_result.cancelled = true;
		m_result.state = OperationState::Cancelled;
		finish();
	} else { m_worker->cancel(); }
}

void CodeFilesPanel::applyResult(const CodeFilesResult& result)
{
	if (!m_busy || pathKey(result.rootPath) != pathKey(m_root)) { return; }
	m_result = result;
	m_selectedPath = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toString() : m_currentPath;
	m_expanded.clear();
	for (int index = 0; index < m_tree->topLevelItemCount(); ++index) {
		const auto* item = m_tree->topLevelItem(index);
		if (item->isExpanded()) { m_expanded.insert(item->text(0)); }
	}
	m_tree->clear();
	m_properties.clear();
	m_directories.clear();
	m_nextFile = 0;
	m_progress->setRange(0, int(result.files.size()));
	m_progress->setValue(0);
	setStatus(tr("Showing %1 project file(s)…").arg(result.files.size()));
	m_rowsTimer->start(0);
}

void CodeFilesPanel::appendRows()
{
	QElapsedTimer elapsed;
	elapsed.start();
	const QSignalBlocker blocker(m_tree);
	const StudioQuery query = parseStudioQuery(m_filter->text());
	int added = 0;
	while (m_nextFile < m_result.files.size() && added++ < 128 && elapsed.elapsed() < 5) {
		const auto& file = m_result.files.at(m_nextFile++);
		const QFileInfo info(file.relativePath);
		QTreeWidgetItem* parent = nullptr;
		if (info.path() != QStringLiteral(".")) {
			parent = m_directories.value(info.path());
			if (!parent) {
				parent = new QTreeWidgetItem(m_tree, {info.path()});
				parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
				parent->setExpanded(m_expanded.contains(info.path()));
				m_directories.insert(info.path(), parent);
			}
		}
		auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
		item->setText(0, info.fileName());
		item->setData(0, Qt::UserRole, file.filePath);
		item->setToolTip(0, QDir::toNativeSeparators(file.filePath));
		const auto properties = codeFileQueryProperties(file);
		m_properties.insert(file.filePath, properties);
		item->setHidden(!studioQueryMatches(query, properties, file.relativePath));
	}
	m_progress->setValue(m_nextFile);
	if (m_nextFile < m_result.files.size()) { m_rowsTimer->start(0); }
	else { finish(); }
}

void CodeFilesPanel::finish()
{
	m_busy = false;
	m_progress->hide();
	m_cancel->setEnabled(false);
	m_cancel->hide();
	if (m_result.files.isEmpty()) {
		placeholder(m_result.cancelled ? tr("The file scan was cancelled.") : !m_result.error.isEmpty() ? m_result.error
			: m_result.complete ? tr("No source files were found.") : tr("The partial scan found no source files."));
	}
	setStatus(m_result.cancelled ? tr("Scan cancelled — %1 file(s). Refresh Tree to try again.").arg(m_result.files.size())
		: !m_result.error.isEmpty() ? tr("File scan failed.")
		: !m_result.complete ? tr("Partial file list — %1 file(s).").arg(m_result.files.size())
		: tr("%1 project file(s).").arg(m_result.files.size()));
	const QString detail = !m_result.error.isEmpty() ? m_result.error : m_result.warnings.join(QLatin1Char('\n'));
	m_details->setText(detail);
	m_details->setVisible(!detail.isEmpty());
	m_tree->sortItems(0, Qt::AscendingOrder);
	followCurrentPath();
	filterFiles();
	if (operationFinished) { operationFinished(m_result); }
}

void CodeFilesPanel::setCurrentPath(const QString& path)
{
	const bool changed = pathKey(path) != pathKey(m_currentPath);
	m_currentPath = path;
	m_selectedPath = path;
	followCurrentPath();
	// A file created elsewhere and then explicitly opened should appear in
	// Files. Ordinary tab switches reuse the catalog, including cancelled scans.
	if (changed && !path.isEmpty() && !m_root.isEmpty() && m_requested && !m_busy && isCodeFileCandidate(path)) {
		const QString relative = QDir(m_root).relativeFilePath(path);
		bool excluded = false;
		const auto folders = QFileInfo(relative).path().split(QLatin1Char('/'));
		for (const auto& folder : folders) { excluded |= isExcludedCodeDirectory(folder); }
		bool known = false;
		for (auto it = m_properties.cbegin(); it != m_properties.cend(); ++it) { if (pathKey(it.key()) == pathKey(path)) { known = true; break; } }
		if (!known && !excluded && !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"))) { refresh(); }
	}
}

void CodeFilesPanel::followCurrentPath()
{
	const QSignalBlocker blocker(m_tree);
	m_tree->clearSelection();
	m_tree->setCurrentItem(nullptr);
	if (m_selectedPath.isEmpty()) { return; }
	for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
		const QString path = (*it)->data(0, Qt::UserRole).toString();
		if (!path.isEmpty() && pathKey(path) == pathKey(m_selectedPath)) {
			m_tree->setCurrentItem(*it);
			m_tree->scrollToItem(*it);
			break;
		}
	}
}

void CodeFilesPanel::filterFiles()
{
	const StudioQuery query = parseStudioQuery(m_filter->text());
	const QStringList unknown = studioQueryUnknownKeys(query, {QStringLiteral("path"), QStringLiteral("name"), QStringLiteral("ext"),
		QStringLiteral("folder"), QStringLiteral("size"), QStringLiteral("language"), QStringLiteral("kind")});
	m_queryStatus->setText(unknown.isEmpty() ? QString() : tr("Unknown filter field(s): %1").arg(unknown.join(QStringLiteral(", "))));
	m_queryStatus->setVisible(!unknown.isEmpty());
	for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
		const QString path = (*it)->data(0, Qt::UserRole).toString();
		if (path.isEmpty()) { continue; }
		const auto properties = m_properties.constFind(path);
		(*it)->setHidden(properties == m_properties.constEnd() || !studioQueryMatches(query, *properties, properties->value(QStringLiteral("path"))));
	}
	for (auto* directory : std::as_const(m_directories)) {
		bool any = false;
		for (int i = 0; i < directory->childCount(); ++i) { any |= !directory->child(i)->isHidden(); }
		directory->setHidden(!any);
		if (any && !query.isEmpty()) { directory->setExpanded(true); }
	}
}

} // namespace vibestudio
