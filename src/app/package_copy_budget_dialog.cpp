#include "app/package_copy_budget_dialog.h"
#include "app/package_copy_sessions_dialog.h"
#include "app/package_action_button.h"
#include "core/studio_settings.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {
PackageCopyBudgetDialog::PackageCopyBudgetDialog(std::shared_ptr<PackageCopyBudget> budget, const QString& directory, QWidget* parent)
	: QDialog(parent), m_budget(std::move(budget))
{
	setObjectName(QStringLiteral("packageCopyBudgetDialog")); setWindowTitle(tr("Temporary Package Copies"));
	setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setAccessibleName(tr("Copy storage usage and limits"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body); outer->addWidget(scroll);
	m_usage = new QLabel; m_usage->setWordWrap(true); m_usage->setTextFormat(Qt::PlainText);
	m_usage->setObjectName(QStringLiteral("packageCopyUsage")); m_usage->setAccessibleName(tr("Reserved temporary copy storage")); layout->addWidget(m_usage);
	auto* note = new QLabel(tr("Reservations cover initial copy payloads, including work in progress. Existing copies remain available until this studio window closes."));
	note->setWordWrap(true); layout->addWidget(note);
	auto* path = new QLineEdit(directory); path->setReadOnly(true); path->setPlaceholderText(tr("No copy directory created yet"));
	path->setObjectName(QStringLiteral("packageCopyDirectory")); path->setAccessibleName(tr("Temporary copy directory"));
	path->setToolTip(QDir::toNativeSeparators(directory)); path->setLayoutDirection(Qt::LeftToRight); path->setCursorPosition(0); layout->addWidget(path);
	auto* review = new PackageActionButton(tr("Review Retained Copies…")); review->setObjectName(QStringLiteral("reviewPackageCopySessions"));
	review->setAccessibleName(review->text()); review->setAutoDefault(false); layout->addWidget(review);
	connect(review, &QPushButton::clicked, this, [this] {
		auto* dialog = new PackageCopySessionsDialog(packageCopyDirectory(), this);
		dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show();
	});
	const auto limits = StudioSettings(StudioSettings::AccessMode::ReadOnly).packageCopyLimits();
	const auto field = [&](const QString& label, const char* name, int maximum, int value) {
		auto* spin = new QSpinBox; spin->setObjectName(QLatin1String(name)); spin->setRange(1, maximum);
		spin->setValue(value); spin->setGroupSeparatorShown(true); spin->setAccessibleName(label);
		auto* caption = new QLabel(label); caption->setWordWrap(true); caption->setBuddy(spin);
		caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum); layout->addWidget(caption); layout->addWidget(spin);
		return spin;
	};
	m_bytes = field(tr("Session payload limit (MiB)"), "packageCopyMaximumMiB", PackageCopyMaximumMiB, limits.maximumBytes / (1024 * 1024));
	m_files = field(tr("Maximum reserved files"), "packageCopyMaximumFiles", PackageCopyMaximumFiles, limits.maximumFiles);
	m_entries = field(tr("Maximum reserved entries, including folders"), "packageCopyMaximumEntries", PackageCopyMaximumEntries, limits.maximumEntries);
	m_batches = field(tr("Maximum retained or pending batches"), "packageCopyMaximumBatches", PackageCopyMaximumBatches, limits.maximumBatches);
	for (auto* spin : {m_bytes, m_files, m_entries, m_batches}) {
		spin->setToolTip(tr("Applies to new copy batches. Lowering a limit preserves existing copies. Use Extract Selected for output you manage yourself."));
	}
	m_status = new QLabel; m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);
	m_status->setObjectName(QStringLiteral("packageCopyBudgetStatus")); m_status->setAccessibleName(tr("Copy storage limit status")); layout->addWidget(m_status); layout->addStretch();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close); outer->addWidget(buttons);
	auto* apply = buttons->button(QDialogButtonBox::Apply); apply->setObjectName(QStringLiteral("applyPackageCopyLimits")); apply->setAccessibleName(tr("Apply temporary copy limits"));
	auto* close = buttons->button(QDialogButtonBox::Close); close->setObjectName(QStringLiteral("closePackageCopyLimits")); close->setAccessibleName(tr("Close temporary copy storage"));
	connect(apply, &QPushButton::clicked, this, &PackageCopyBudgetDialog::applyLimits);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* timer = new QTimer(this); timer->setInterval(250); connect(timer, &QTimer::timeout, this, &PackageCopyBudgetDialog::updateUsage); timer->start();
	resize(QSize(700, 680).boundedTo(screen()->availableGeometry().size() - QSize(40, 40))); updateUsage();
}
PackageCopyLimits PackageCopyBudgetDialog::proposedLimits() const
{
	return {static_cast<quint64>(m_bytes->value()) * 1024 * 1024, m_files->value(), m_entries->value(), m_batches->value()};
}
void PackageCopyBudgetDialog::applyLimits()
{
	StudioSettings settings; settings.setPackageCopyLimits(proposedLimits()); settings.sync();
	m_status->setText(settings.status() == QSettings::NoError && !settings.isReadOnly()
		? tr("Limits saved. New copy batches use these limits; existing copies are preserved.")
		: tr("The temporary copy limits could not be saved."));
	updateUsage();
}
void PackageCopyBudgetDialog::updateUsage()
{
	const auto usage = m_budget ? m_budget->usage() : PackageCopyUsage();
	const auto number = [&](qint64 value) { return QChar(0x2068) + locale().toString(value) + QChar(0x2069); };
	QString message = tr("Reserved payload: %1 MiB\nFiles: %2 · Entries: %3\nBatches: %4 · In progress: %5")
		.arg(QChar(0x2068) + locale().toString(static_cast<double>(usage.bytes) / (1024 * 1024), 'f', 1) + QChar(0x2069),
			number(usage.files), number(usage.entries), number(usage.batches), number(usage.pendingBatches));
	if (usage.cleanupFailedBatches) {
		message += QLatin1Char('\n') + tr("Cleanup failures: %1. Full reservations remain charged.").arg(number(usage.cleanupFailedBatches));
	}
	const auto limits = StudioSettings(StudioSettings::AccessMode::ReadOnly).packageCopyLimits();
	if (usage.bytes > limits.maximumBytes || usage.files > limits.maximumFiles || usage.entries > limits.maximumEntries || usage.batches >= limits.maximumBatches) {
		message += QLatin1Char('\n') + tr("Reservations have reached a limit. Adjust limits or use Extract Selected; existing copies remain available.");
	}
	if (m_usage->text() != message) { m_usage->setText(message); }
}
} // namespace vibestudio
