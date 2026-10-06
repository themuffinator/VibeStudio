#pragma once

#include "core/package_copy_budget.h"
#include <QCoreApplication>
#include <QDialog>

class QLabel;
class QSpinBox;

namespace vibestudio {
class PackageCopyBudgetDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageCopyBudgetDialog)
public:
	explicit PackageCopyBudgetDialog(std::shared_ptr<PackageCopyBudget> budget, const QString& directory, QWidget* parent = nullptr);
private:
	PackageCopyLimits proposedLimits() const;
	void updateUsage();
	void applyLimits();
	std::shared_ptr<PackageCopyBudget> m_budget;
	QSpinBox *m_bytes = nullptr, *m_files = nullptr, *m_entries = nullptr, *m_batches = nullptr;
	QLabel *m_usage = nullptr, *m_status = nullptr;
};
} // namespace vibestudio
