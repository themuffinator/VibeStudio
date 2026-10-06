#pragma once

#include "core/package_staging.h"

#include <QListView>

namespace vibestudio {

struct PackageStagingRow {
	QString text, state;
	bool enabled = true;
	bool operator==(const PackageStagingRow&) const = default;
};

class PackageStagingListModel;

// Prepared operation/conflict vectors remain implicitly shared. Only visible
// rows format their presentation; bounded overview/composition rows can wrap.
class PackageStagingView final : public QListView {
	Q_OBJECT
public:
	explicit PackageStagingView(QWidget* parent = nullptr);
	void setContents(QVector<PackageStagingRow> before, QVector<PackageStageOperation> operations,
		QVector<PackageStageConflict> conflicts, QVector<PackageStagingRow> after);
	void showMessage(const QString& message);
	void refreshPresentation();
	[[nodiscard]] QStringList selectedOperationIds() const;
	[[nodiscard]] QString currentDetails() const;
signals:
	void detailsChanged(const QString& text);
private:
	PackageStagingListModel* m_rows = nullptr;
};

} // namespace vibestudio
