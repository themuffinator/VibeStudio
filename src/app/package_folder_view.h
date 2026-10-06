#pragma once

#include "core/package_browser.h"

#include <QTreeView>
#include <memory>

namespace vibestudio {

class PackageFolderModel;

// Native tree roles over the same immutable index as the package entry list.
// Installing/selecting a folder never scans archive entries or builds item objects.
class PackageFolderView final : public QTreeView {
	Q_OBJECT
public:
	explicit PackageFolderView(QWidget* parent = nullptr);
	void showFolders(std::shared_ptr<const PackageBrowserIndex> index, const QString& revision,
		const QString& rootLabel, const QString& rootDetail, bool rootWarning);
	void showMessage(const QString& message, const QString& detail = {});
	[[nodiscard]] bool readyFor(const QString& revision) const;
	[[nodiscard]] QModelIndex folderIndex(const QString& path) const;
	[[nodiscard]] QString selectedFolder() const;
	bool selectFolder(const QString& path);
	void refreshPresentation();
	void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;

signals:
	void folderSelectionChanged();

protected:
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void updateColumnWidth(const QModelIndex& index, bool children = false);
	int m_seenDepth = 0;
	QString m_widestCaption;
	PackageFolderModel* m_folders = nullptr;
	QString m_revision;
};

} // namespace vibestudio
