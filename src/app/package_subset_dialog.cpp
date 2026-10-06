#include "app/package_subset_dialog.h"
#include "app/package_action_button.h"
#include "core/studio_settings.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {
struct PackageSubsetDialog::Work {
	std::atomic_bool cancel{false};
	QMutex mutex;
	int done = 0, total = 0;
	QString path;
};
PackageSubsetDialog::PackageSubsetDialog(std::shared_ptr<const PackageArchiveReader> archive, PackageSelectionRequest selection, QWidget* parent)
	: QDialog(parent), m_archive(std::move(archive)), m_selection(std::move(selection)), m_work(std::make_shared<Work>())
{
	setObjectName(QStringLiteral("packageSubsetDialog")); setWindowTitle(tr("Export Package Subset")); setAccessibleName(tr("Review and export package subset"));
	setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal); if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(tr("Selected occurrences and required package members"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize); scroll->setWidget(body); outer->addWidget(scroll, 1);
	m_status = new QLabel(tr("Preparing the selected package snapshot…")); m_status->setObjectName(QStringLiteral("packageSubsetStatus"));
	m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Package subset review status")); layout->addWidget(m_status);
	m_pageControls = new QWidget; auto* pageLayout = new QVBoxLayout(m_pageControls); pageLayout->setContentsMargins(0, 0, 0, 0);
	auto* pageLabel = new QLabel(tr("Review page")); pageLabel->setWordWrap(true); m_page = new QSpinBox; m_page->setObjectName(QStringLiteral("packageSubsetPage"));
	m_page->setAccessibleName(tr("Subset review page")); m_page->setToolTip(tr("Each page displays up to 500 entries. All reviewed pages are exported."));
	pageLabel->setBuddy(m_page); pageLayout->addWidget(pageLabel); pageLayout->addWidget(m_page); layout->addWidget(m_pageControls); m_pageControls->hide();
	m_files = new QTableWidget(0, 3); m_files->setObjectName(QStringLiteral("packageSubsetFiles")); m_files->setAccessibleName(tr("Files and groups included in the export"));
	m_files->setHorizontalHeaderLabels({tr("Entry"), tr("Source entry"), tr("Included for")});
	for (int column = 0; column < m_files->columnCount(); ++column) { m_files->horizontalHeaderItem(column)->setToolTip(m_files->horizontalHeaderItem(column)->text()); }
	m_files->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); m_files->horizontalHeader()->setTextElideMode(Qt::ElideRight);
	m_files->setTextElideMode(Qt::ElideMiddle); m_files->setEditTriggers(QAbstractItemView::NoEditTriggers); m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_files->setMinimumHeight(fontMetrics().height() * 10); layout->addWidget(m_files);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packageSubsetDetails")); m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Subset source, warnings and export results")); m_details->setMinimumHeight(fontMetrics().height() * 6); layout->addWidget(m_details);
	m_progress = new QProgressBar; m_progress->setAccessibleName(tr("Package subset progress")); layout->addWidget(m_progress);
	m_export = new PackageActionButton(tr("Export Reviewed Subset…")); m_export->setObjectName(QStringLiteral("packageSubsetExport"));
	m_cancel = new PackageActionButton(tr("Cancel Operation")); m_cancel->setObjectName(QStringLiteral("packageSubsetCancel"));
	for (auto* button : {m_export, m_cancel}) { button->setAccessibleName(button->text()); button->setAutoDefault(false); layout->addWidget(button); }
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); buttons->button(QDialogButtonBox::Close)->setAccessibleName(tr("Close package subset review")); outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &PackageSubsetDialog::reject);
	connect(m_export, &QPushButton::clicked, this, &PackageSubsetDialog::exportSubset);
	connect(m_page, &QSpinBox::valueChanged, this, [this]() { showPage(); });
	connect(m_cancel, &QPushButton::clicked, this, [this]() { m_work->cancel = true; m_cancel->setEnabled(false); m_status->setText(tr("Stopping the package operation safely…")); });
	connect(m_files, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
		const qsizetype index = (m_page->value() - 1) * 500 + row;
		if (row < 0 || index < 0 || index >= m_review.members.size()) { return; }
		const auto& entry = m_review.members.at(index);
		m_details->setPlainText(tr("Source: %1\nEntry: %2\nSnapshot index: %3\nIncluded for: %4")
			.arg(m_archive->sourcePath(), entry.virtualPath, locale().toString(static_cast<qlonglong>(entry.entryIndex)), entry.selected ? tr("Selected") : entry.reason)
			+ QLatin1Char('\n') + m_review.warnings.join(QLatin1Char('\n')));
	});
	auto* timer = new QTimer(this); timer->setInterval(100);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (!busy()) { return; } QMutexLocker lock(&m_work->mutex);
		m_progress->setRange(0, m_work->total); m_progress->setValue(m_work->done); m_progress->setAccessibleDescription(m_work->path);
	}); timer->start();
	resize(QSize(920, 700).boundedTo(screen()->availableGeometry().size() - QSize(40, 40))); updateControls();
	QTimer::singleShot(0, this, &PackageSubsetDialog::prepare);
}
PackageSubsetDialog::~PackageSubsetDialog()
{
	m_work->cancel = true; if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool PackageSubsetDialog::busy() const { return m_thread != nullptr; }
void PackageSubsetDialog::reject()
{
	if (busy()) { m_closeRequested = true; m_work->cancel = true; m_cancel->setEnabled(false); m_status->setText(tr("Stopping the package operation safely…")); return; }
	QDialog::reject();
}
void PackageSubsetDialog::updateControls()
{
	m_export->setEnabled(!busy() && m_ready); m_cancel->setEnabled(busy() && !m_work->cancel.load());
}
void PackageSubsetDialog::start(std::function<void()> work, std::function<void()> complete)
{
	if (busy()) { return; } m_work->cancel = false;
	{ QMutexLocker lock(&m_work->mutex); m_work->done = 0; m_work->total = 0; m_work->path.clear(); }
	m_thread = QThread::create(std::move(work)); setProperty("operationRunning", true); m_progress->setRange(0, 0); m_progress->show(); updateControls();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; setProperty("operationRunning", false); m_progress->hide();
		complete(); if (m_closeRequested) { QDialog::reject(); return; } updateControls();
	}); m_thread->start();
}
void PackageSubsetDialog::prepare()
{
	struct Result { PackageStagingModel plan; PackageSubsetReview review; QString error; bool ready = false; };
	auto result = std::make_shared<Result>(); const auto state = m_work;
	start([archive = m_archive, request = m_selection, state, result]() {
		if (!archive) { result->error = tr("No package snapshot is available."); return; }
		PackageReadControl control; control.isCancelled = [state]() { return state->cancel.load(); };
		const auto selected = selectPackageFiles(*archive, request, control);
		if (!selected.succeeded()) { result->error = selected.errors.join(QLatin1Char('\n')); return; }
		result->ready = result->plan.loadBaseArchiveSubsetAt(*archive, selected.entryIndexes, &result->error, &result->review, control);
	}, [this, result]() {
		m_ready = result->ready; if (!m_ready) { m_status->setText(result->error); return; }
		m_plan = std::move(result->plan); m_review = std::move(result->review);
		const int pages = static_cast<int>((m_review.members.size() + 499) / 500);
		m_page->setRange(1, qMax(1, pages)); m_page->setSuffix(tr(" of %1").arg(locale().toString(pages))); m_pageControls->setVisible(pages > 1);
		int selected = 0;
		for (const auto& member : m_review.members) { selected += member.selected; }
		QString text = tr("%n selected file(s).", nullptr, selected) + QLatin1Char(' ') + tr("%n required group member(s) included.", nullptr, static_cast<int>(m_review.members.size()) - selected);
		if (!m_review.warnings.isEmpty()) { text += QLatin1Char('\n') + m_review.warnings.join(QLatin1Char('\n')); }
		m_status->setText(text); showPage();
	});
}
void PackageSubsetDialog::showPage()
{
	m_files->clearSelection(); m_files->setCurrentCell(-1, -1);
	const qsizetype first = (m_page->value() - 1) * 500;
	const int shown = static_cast<int>(qBound(qsizetype{0}, m_review.members.size() - first, qsizetype{500})); m_files->setRowCount(shown);
	for (int row = 0; row < shown; ++row) {
		const auto& member = m_review.members.at(first + row); const QString why = member.selected ? tr("Selected") : member.reason;
		const QString ordinal = member.sourceOrdinal >= 0 ? locale().toString(member.sourceOrdinal + 1) : tr("New entry");
		const QStringList cells{member.virtualPath, ordinal, why};
		for (int column = 0; column < cells.size(); ++column) {
			auto* item = new QTableWidgetItem(QChar(0x2068) + cells.at(column) + QChar(0x2069)); item->setToolTip(cells.at(column));
			item->setData(Qt::AccessibleTextRole, tr("%1, source entry %2, %3").arg(member.virtualPath, ordinal, why)); m_files->setItem(row, column, item);
		}
	}
	if (shown) { m_files->setCurrentCell(0, 0); m_files->scrollToTop(); }
}
void PackageSubsetDialog::exportSubset()
{
	if (!m_ready || busy()) { return; }
	const bool wad = m_plan.sourceFormat() == PackageArchiveFormat::Wad;
	const QString filter = wad ? tr("WAD Packages (*.wad);;PK3 Packages (*.pk3);;ZIP Packages (*.zip);;PAK Packages (*.pak)")
		: tr("PK3 Packages (*.pk3);;ZIP Packages (*.zip);;PAK Packages (*.pak);;WAD Packages (*.wad)");
	QString chosenFilter;
	QString output = QFileDialog::getSaveFileName(this, tr("Export Reviewed Package Subset"), QStringLiteral("subset.") + (wad ? QStringLiteral("wad") : QStringLiteral("pk3")),
		filter, &chosenFilter, QFileDialog::DontConfirmOverwrite);
	if (output.isEmpty()) { return; }
	if (QFileInfo(output).suffix().isEmpty()) {
		const auto extension = QRegularExpression(QStringLiteral("\\*\\.([a-z0-9]+)")).match(chosenFilter);
		if (extension.hasMatch()) { output += QLatin1Char('.') + extension.captured(1); }
	}
	if (m_archive->protectsInputPath(output)) { m_status->setText(tr("Choose a separate output path. A subset cannot replace its source or staged inputs.")); return; }
	const bool overwrite = QFileInfo::exists(output);
	if (overwrite && QMessageBox::question(this, tr("Replace Existing Package?"), tr("Replace %1 with the reviewed subset?").arg(QDir::toNativeSeparators(output)),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) { return; }
	StudioSettings().rememberPackagePublicationDirectory(QFileInfo(output).absolutePath());
	auto report = std::make_shared<PackageWriteReport>(); const auto state = m_work;
	m_status->setText(tr("Writing the reviewed package subset…"));
	start([plan = m_plan, state, report, output, overwrite]() {
		PackageWriteRequest request; request.destinationPath = output; request.allowOverwrite = overwrite;
		request.isCancelled = [state]() { return state->cancel.load(); };
		request.progress = [state](int done, int total, const QString& path) { QMutexLocker lock(&state->mutex); state->done = done; state->total = total; state->path = path; };
		*report = plan.writeArchive(request);
	}, [this, report]() {
		m_status->setText(report->succeeded() ? tr("Exported %n file(s).", nullptr, report->entryCount)
			: report->cancelled ? tr("Package subset export cancelled.") : tr("Package subset export failed. See the details."));
		m_details->setPlainText(packageWriteReportText(*report)); if (exported) { exported(*report); }
	});
}
} // namespace vibestudio
