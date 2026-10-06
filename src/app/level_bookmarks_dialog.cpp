#include "app/level_bookmarks_dialog.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QUuid>

namespace vibestudio {
LevelBookmarksDialog::LevelBookmarksDialog(LevelViewBookmarks bookmarks, Capture capture, Restore restore,
	Commit commit, Reload reload, bool readOnly, QWidget* parent)
	: QDialog(parent), m_bookmarks(std::move(bookmarks)), m_capture(std::move(capture)), m_restore(std::move(restore)),
	  m_commit(std::move(commit)), m_reload(std::move(reload))
{
	m_readOnly = readOnly;
	setObjectName(QStringLiteral("levelBookmarksDialog"));
	setWindowTitle(tr("Saved Level Views"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Manage camera and plan views for the current map. Save applies changes to the view list; Go to View changes navigation immediately."));
	resize(680, 560);
	auto* layout = new QVBoxLayout(this);
	m_list = new QListWidget;
	m_list->setObjectName(QStringLiteral("levelBookmarkList"));
	m_list->setAccessibleName(tr("Saved views"));
	layout->addWidget(m_list, 1);
	m_detail = new QLabel;
	m_detail->setWordWrap(true);
	m_detail->setTextFormat(Qt::PlainText);
	m_detail->setAccessibleName(tr("View details"));
	layout->addWidget(m_detail);
	auto* form = new QFormLayout;
	m_name = new QLineEdit;
	m_name->setObjectName(QStringLiteral("levelBookmarkName"));
	m_name->setMaxLength(128);
	m_name->setAccessibleName(tr("View name"));
	m_name->setReadOnly(readOnly);
	form->addRow(tr("&Name"), m_name);
	layout->addLayout(form);
	auto* actions = new QGridLayout;
	const QStringList labels {tr("Capture Current"), tr("Update View"), tr("Rename"), tr("Remove"), tr("Go to View"), tr("Reload Saved List")};
	const QStringList ids {"levelBookmarkCapture", "levelBookmarkUpdate", "levelBookmarkRename", "levelBookmarkRemove", "levelBookmarkGo", "levelBookmarkReload"};
	const QStringList descriptions {
		tr("Add the current camera, plans and layout using this name."), tr("Replace the selected view with the current camera, plans and layout."),
		tr("Rename the selected view."), tr("Remove the selected view from this list."), tr("Restore the selected view without changing the map or its selection."),
		tr("Discard list edits and read the latest saved views from storage.")};
	for (int i = 0; i < labels.size(); ++i) {
		auto* button = new QPushButton(labels[i]);
		button->setObjectName(ids[i]);
		button->setAccessibleName(labels[i]);
		button->setAccessibleDescription(descriptions[i]);
		button->setToolTip(descriptions[i]);
		button->setAutoDefault(false);
		actions->addWidget(button, i / 3, i % 3);
		connect(button, &QPushButton::clicked, this, [this, i] { change(i); });
		if (i > 0 && i < 5) {
			auto updateEnabled = [this, button, i, readOnly] { button->setEnabled(m_list->currentRow() >= 0 && (!readOnly || i == 4)); };
			connect(m_list, &QListWidget::currentRowChanged, this, updateEnabled);
			updateEnabled();
		} else if (i == 0) { button->setEnabled(!readOnly); }
	}
	layout->addLayout(actions);
	auto* files = new QDialogButtonBox;
	auto* import = files->addButton(tr("Import…"), QDialogButtonBox::ActionRole);
	import->setObjectName(QStringLiteral("levelBookmarkImport"));
	import->setEnabled(!readOnly);
	import->setToolTip(tr("Replace this list from a level views file. Save commits the imported list."));
	auto* exportButton = files->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
	exportButton->setObjectName(QStringLiteral("levelBookmarkExport"));
	layout->addWidget(files);
	connect(import, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getOpenFileName(this, tr("Import Level Views"), {}, tr("Level views (*.vviews *.json)"));
		if (!path.isEmpty()) { importFile(path); }
	});
	connect(exportButton, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, tr("Export Level Views"), QStringLiteral("level.vviews"), tr("Level views (*.vviews)"));
		if (!path.isEmpty()) { exportFile(path, true); }
	});
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("levelBookmarkStatus"));
	m_status->setAccessibleName(tr("Saved view status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Save)->setEnabled(!readOnly);
	buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("levelBookmarkSave"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		QString error;
		if (!m_commit(m_bookmarks, &error)) { report(error); return; }
		accept();
	});
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row < 0 || row >= m_bookmarks.size()) { m_name->clear(); m_detail->clear(); return; }
		const auto& bookmark = m_bookmarks[row];
		m_name->setText(bookmark.name);
		m_detail->setText(tr("%1 · %2 · Active plan %3").arg(levelViewLayoutDisplayName(bookmark.view.layout),
			bookmark.view.camera.perspective ? tr("Perspective camera") : tr("Orbit camera")).arg(bookmark.view.activePlan + 1));
	});
	connect(m_list, &QListWidget::itemActivated, this, [this] { change(4); });
	refresh();
	if (readOnly) { report(tr("Settings are read-only. You can navigate and export saved views.")); }
}
void LevelBookmarksDialog::report(const QString& error) { m_status->setText(error); }
void LevelBookmarksDialog::refresh(const QString& selected)
{
	m_list->clear();
	int row = -1;
	for (const auto& bookmark : m_bookmarks) {
		m_list->addItem(bookmark.name);
		if (bookmark.id == selected) { row = m_list->count() - 1; }
	}
	m_list->setCurrentRow(row < 0 && !m_bookmarks.isEmpty() ? 0 : row);
}
void LevelBookmarksDialog::change(int operation)
{
	if (m_readOnly && operation < 4) { return; }
	QString error;
	if (operation == 5) {
		LevelViewBookmarks loaded;
		if (!m_reload(&loaded, &error)) { report(error); return; }
		m_bookmarks = std::move(loaded);
		refresh();
		report(tr("Saved views reloaded."));
		return;
	}
	const int row = m_list->currentRow();
	if (operation != 0 && (row < 0 || row >= m_bookmarks.size())) { return; }
	if (operation == 4) {
		if (!m_restore(m_bookmarks[row].view, &error)) { report(error); return; }
		report(tr("Restored %1.").arg(m_bookmarks[row].name));
		return;
	}
	auto candidate = m_bookmarks;
	QString selected;
	if (operation == 0 || operation == 1) {
		LevelViewState view;
		if (!m_capture(&view, &error)) { report(error); return; }
		if (operation == 0) {
			selected = QUuid::createUuid().toString(QUuid::WithoutBraces);
			candidate.append({selected, m_name->text().trimmed(), view});
		} else { candidate[row].view = view; selected = candidate[row].id; }
	} else if (operation == 2) {
		candidate[row].name = m_name->text().trimmed();
		selected = candidate[row].id;
	} else if (operation == 3) { candidate.removeAt(row); }
	if (!validateLevelBookmarks(candidate, &error)) { report(error); return; }
	m_bookmarks = std::move(candidate);
	refresh(selected);
	report(tr("View list changed. Save applies these changes."));
}
bool LevelBookmarksDialog::importFile(const QString& path)
{
	if (m_readOnly) { report(tr("Settings are read-only. You can navigate and export saved views.")); return false; }
	LevelViewBookmarks imported;
	QByteArray revision;
	QString error;
	if (!QFileInfo::exists(path) || !readLevelBookmarks(path, &imported, &revision, &error)) {
		report(error.isEmpty() ? tr("The level views file does not exist.") : error);
		return false;
	}
	m_bookmarks = std::move(imported);
	refresh();
	report(tr("Imported %n saved view(s). Save replaces the stored list.", nullptr, m_bookmarks.size()));
	return true;
}
bool LevelBookmarksDialog::exportFile(const QString& path, bool overwrite)
{
	QString error;
	LevelViewBookmarks current;
	QByteArray revision;
	if (QFileInfo::exists(path) && !overwrite) { report(tr("The destination exists. Choose another file or confirm replacement.")); return false; }
	if (!readLevelBookmarks(path, &current, &revision, &error) || !writeLevelBookmarks(path, m_bookmarks, revision, nullptr, &error)) {
		report(error); return false;
	}
	report(tr("Exported %n saved view(s).", nullptr, m_bookmarks.size()));
	return true;
}
} // namespace vibestudio
