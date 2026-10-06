#include "app/package_draft_storage_dialog.h"
#include "app/package_action_button.h"
#include "core/studio_settings.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {

PackageDraftStorageDialog::PackageDraftStorageDialog(QString path, QWidget* parent)
	: QDialog(parent), m_path(std::move(path)), m_cancel(std::make_shared<std::atomic_bool>(false))
{
	setObjectName(QStringLiteral("packageDraftStorageDialog")); setWindowTitle(tr("Saved Package Draft Storage"));
	setAccessibleName(tr("Saved package draft storage manager")); setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(tr("Draft storage limits and reviewed files"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body); outer->addWidget(scroll, 1);
	m_directory = new QLineEdit(QDir::toNativeSeparators(m_path)); m_directory->setReadOnly(true); m_directory->setAccessibleName(tr("Selected package draft directory"));
	layout->addWidget(m_directory);
	m_choose = new PackageActionButton(tr("Choose Draft…")); m_choose->setObjectName(QStringLiteral("packageDraftStorageChoose")); layout->addWidget(m_choose);
	const StudioSettings settings;
	auto* bytesLabel = new QLabel(tr("Maximum storage per draft (MiB)")); bytesLabel->setWordWrap(true); layout->addWidget(bytesLabel);
	auto* bytes = new QSpinBox; bytes->setObjectName(QStringLiteral("packageDraftMaximumMiB")); bytes->setRange(128, 131072);
	bytes->setValue(settings.packageDraftMaximumMiB()); bytes->setGroupSeparatorShown(true); bytes->setAccessibleName(bytesLabel->text()); bytesLabel->setBuddy(bytes);
	bytes->setToolTip(tr("Includes payloads, unused objects and space for the next metadata commit. Lowering the limit preserves existing drafts.")); layout->addWidget(bytes);
	auto* filesLabel = new QLabel(tr("Maximum files per draft")); filesLabel->setWordWrap(true); layout->addWidget(filesLabel);
	auto* files = new QSpinBox; files->setObjectName(QStringLiteral("packageDraftMaximumFiles")); files->setRange(1, PackageStorageEntryLimit);
	files->setValue(settings.packageDraftMaximumFiles()); files->setGroupSeparatorShown(true); files->setAccessibleName(filesLabel->text()); filesLabel->setBuddy(files); layout->addWidget(files);
	connect(bytes, &QSpinBox::valueChanged, this, [this](int value) { StudioSettings().setPackageDraftMaximumMiB(value); updateUsage(); });
	connect(files, &QSpinBox::valueChanged, this, [this](int value) { StudioSettings().setPackageDraftMaximumFiles(value); updateUsage(); });
	m_usage = new QLabel; m_usage->setObjectName(QStringLiteral("packageDraftStorageUsage")); m_usage->setWordWrap(true); m_usage->setTextFormat(Qt::PlainText);
	m_usage->setAccessibleName(tr("Draft storage usage and reclaimable space")); layout->addWidget(m_usage);
	m_status = new QLabel(tr("Choose a saved .vibepackage directory to review its storage.")); m_status->setObjectName(QStringLiteral("packageDraftStorageStatus"));
	m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Draft storage operation status")); layout->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setAccessibleName(tr("Draft storage review progress")); m_progress->hide(); layout->addWidget(m_progress);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packageDraftStorageDetails")); m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Reviewed unused files and storage checksums")); m_details->setMinimumHeight(fontMetrics().height() * 8); layout->addWidget(m_details);
	m_compact = new PackageActionButton(tr("Reclaim Reviewed Space…")); m_compact->setObjectName(QStringLiteral("packageDraftStorageCompact"));
	m_compact->setToolTip(tr("Close this draft and wait for its background readers before reclaiming unused objects. Undo and redo content is retained."));
	m_refresh = new PackageActionButton(tr("Refresh")); m_refresh->setObjectName(QStringLiteral("packageDraftStorageRefresh"));
	for (auto* button : {m_choose, m_compact, m_refresh}) { button->setAccessibleName(button->text()); button->setAutoDefault(false); }
	layout->addWidget(m_compact); layout->addWidget(m_refresh);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); buttons->button(QDialogButtonBox::Close)->setAccessibleName(tr("Close draft storage manager")); outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &PackageDraftStorageDialog::reject);
	connect(m_refresh, &QPushButton::clicked, this, [this]() { reload(); }); connect(m_compact, &QPushButton::clicked, this, &PackageDraftStorageDialog::compact);
	connect(m_choose, &QPushButton::clicked, this, [this]() {
		const auto path = QFileDialog::getExistingDirectory(this, tr("Choose Package Draft"), m_path, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
		if (path.isEmpty()) { return; } m_path = path; m_directory->setText(QDir::toNativeSeparators(path)); m_review = {}; reload();
	});
	resize(QSize(880, 740).boundedTo(screen()->availableGeometry().size() - QSize(40, 40))); updateControls();
	if (!m_path.isEmpty()) { QTimer::singleShot(0, this, [this]() { reload(); }); }
}

PackageDraftStorageDialog::~PackageDraftStorageDialog()
{
	m_cancel->store(true); if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool PackageDraftStorageDialog::busy() const { return m_thread != nullptr; }
void PackageDraftStorageDialog::reject()
{
	if (busy()) { m_closeRequested = true; m_cancel->store(true); m_status->setText(tr("Stopping the draft storage operation…")); return; } QDialog::reject();
}
void PackageDraftStorageDialog::start(std::function<void()> work, std::function<void()> complete)
{
	if (busy()) { return; } m_cancel->store(false); m_thread = QThread::create(std::move(work)); m_progress->show(); updateControls();
	setProperty("operationRunning", true);
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; m_progress->hide(); setProperty("operationRunning", false);
		if (m_closeRequested) { QDialog::reject(); return; } complete(); updateControls();
	}); m_thread->start();
}
void PackageDraftStorageDialog::reload(const QString& notice)
{
	if (busy() || m_path.isEmpty()) { return; }
	m_status->setText(tr("Verifying draft content, history and storage…")); auto review = std::make_shared<PackageDraftStorageReview>();
	start([review, path = m_path, cancel = m_cancel]() { *review = reviewPackageDraftStorage(path, {[cancel]() { return cancel->load(); }, {}, {}, {}}); }, [this, review, notice]() {
		m_review = *review; updateUsage();
		QStringList status; if (!notice.isEmpty()) { status << notice; }
		status << (m_review.complete() ? tr("Content and history verified. Close the draft and wait for readers before reclaiming unused files.") : m_review.error);
		m_status->setText(status.join(QLatin1Char('\n')));
		QStringList details{tr("Manifest SHA-256: %1").arg(QString::fromLatin1(m_review.storage.manifestSha256.toHex())),
			tr("Storage review SHA-256: %1").arg(QString::fromLatin1(m_review.storage.fingerprint.toHex()))};
		const auto shown = qMin<qsizetype>(100, m_review.reclaimable.size());
		for (qsizetype i = 0; i < shown; ++i) {
			const auto& file = m_review.reclaimable.at(i); details << tr("%1 · %2 bytes").arg(file.relativePath, locale().toString(file.bytes));
		}
		if (shown < m_review.reclaimable.size()) { details << tr("%n additional unused file(s). Full details are available through the CLI.", nullptr, static_cast<int>(m_review.reclaimable.size() - shown)); }
		m_details->setPlainText(details.join(QLatin1Char('\n')));
	});
}
void PackageDraftStorageDialog::updateUsage()
{
	if (!m_review.complete()) { m_usage->setText(tr("Storage totals require a complete review.")); return; }
	const StudioSettings settings;
	m_usage->setText(tr("Storage: %1 of %2 MiB · %3 of %4 files.")
		.arg(locale().toString(m_review.storage.bytes / (1024.0 * 1024.0), 'f', 2)).arg(locale().toString(settings.packageDraftMaximumMiB()))
		.arg(locale().toString(m_review.storage.files.size())).arg(locale().toString(settings.packageDraftMaximumFiles()))
		+ QLatin1Char('\n') + tr("Reclaimable: %1 MiB in %n file(s).", nullptr, static_cast<int>(m_review.reclaimable.size()))
			.arg(locale().toString(m_review.reclaimableBytes / (1024.0 * 1024.0), 'f', 2)));
}
void PackageDraftStorageDialog::updateControls()
{
	m_choose->setEnabled(!busy()); m_refresh->setEnabled(!busy() && !m_path.isEmpty());
	m_compact->setEnabled(!busy() && m_review.complete() && !m_review.reclaimable.isEmpty());
}
void PackageDraftStorageDialog::compact()
{
	if (busy() || !m_review.complete() || m_review.reclaimable.isEmpty()) { return; }
	if (QMessageBox::question(this, tr("Reclaim Draft Storage"),
		tr("Remove %n unused file(s) (%1 MiB) from the reviewed draft?", nullptr, static_cast<int>(m_review.reclaimable.size()))
			.arg(locale().toString(m_review.reclaimableBytes / (1024.0 * 1024.0), 'f', 2)), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) { return; }
	m_status->setText(tr("Reclaiming reviewed unused files…")); auto result = std::make_shared<PackageDraftCompaction>();
	start([result, path = m_path, hash = m_review.storage.fingerprint, cancel = m_cancel]() {
		*result = compactPackageDraftStorage(path, hash, false, {[cancel]() { return cancel->load(); }, {}, {}, {}});
	}, [this, result]() {
		QString notice = result->succeeded ? tr("Unused space reclaimed. Draft content and edit history were preserved.") : result->error;
		if (!result->succeeded && result->reclaimedFiles) { notice += QLatin1Char('\n') + tr("%n unused file(s) removed before stopping.", nullptr, result->reclaimedFiles); }
		reload(notice);
	});
}

} // namespace vibestudio
