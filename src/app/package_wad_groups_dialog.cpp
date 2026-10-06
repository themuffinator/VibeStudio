#include "app/package_wad_groups_dialog.h"
#include "app/package_action_button.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {
PackageWadGroupsDialog::PackageWadGroupsDialog(PackageStagingModel model, QWidget* parent)
	: QDialog(parent), m_original(std::move(model)), m_cancelled(std::make_shared<std::atomic_bool>(false))
{
	setObjectName(QStringLiteral("packageWadGroupsDialog")); setWindowTitle(tr("WAD Groups")); setAccessibleName(tr("Review WAD group edits"));
	setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal); if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(tr("WAD group editing controls")); auto* body = new QWidget; auto* layout = new QVBoxLayout(body);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize); scroll->setWidget(body); outer->addWidget(scroll, 1);
	m_status = new QLabel(tr("Inspecting WAD groups…")); m_status->setObjectName(QStringLiteral("packageGroupsStatus"));
	m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("WAD group operation status")); layout->addWidget(m_status);
	const auto field = [&](const QString& title, QWidget* widget) {
		auto* label = new QLabel(title); label->setWordWrap(true); label->setBuddy(widget); widget->setAccessibleName(title); layout->addWidget(label); layout->addWidget(widget);
	};
	m_groups = new QComboBox; m_groups->setObjectName(QStringLiteral("packageGroupsSelection")); m_groups->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_groups->setMinimumContentsLength(12); field(tr("Group"), m_groups);
	m_operation = new QComboBox; m_operation->setObjectName(QStringLiteral("packageGroupsOperation")); m_operation->addItems({tr("Rename map"), tr("Delete complete group")}); field(tr("Operation"), m_operation);
	m_target = new QLineEdit; m_target->setObjectName(QStringLiteral("packageGroupsTarget")); m_target->setMaxLength(8);
	m_target->setToolTip(tr("Use a non-reserved map label. A GL companion limits the main label to five characters.")); field(tr("New main map label"), m_target);
	m_prepare = new PackageActionButton(tr("Review Edit")); m_prepare->setObjectName(QStringLiteral("packageGroupsReview"));
	m_prepare->setAccessibleName(m_prepare->text()); m_prepare->setAutoDefault(false); layout->addWidget(m_prepare);
	m_page = new QSpinBox; m_page->setObjectName(QStringLiteral("packageGroupsPage")); m_page->setRange(1, 1); field(tr("Changes page"), m_page);
	m_page->setToolTip(tr("Each page shows up to 500 changes. Apply includes every reviewed page."));
	m_changes = new QTableWidget(0, 3); m_changes->setObjectName(QStringLiteral("packageGroupsChanges")); m_changes->setAccessibleName(tr("Exact WAD group changes"));
	m_changes->setHorizontalHeaderLabels({tr("Source entry"), tr("Before"), tr("After")}); m_changes->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	for (int column = 0; column < m_changes->columnCount(); ++column) { m_changes->horizontalHeaderItem(column)->setToolTip(m_changes->horizontalHeaderItem(column)->text()); }
	m_changes->horizontalHeader()->setTextElideMode(Qt::ElideRight); m_changes->setTextElideMode(Qt::ElideMiddle);
	m_changes->setEditTriggers(QAbstractItemView::NoEditTriggers); m_changes->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_changes->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_changes);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packageGroupsDetails")); m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("WAD group selection and change details")); m_details->setMinimumHeight(fontMetrics().height() * 4); layout->addWidget(m_details);
	m_apply = new PackageActionButton(tr("Apply Reviewed Edit")); m_apply->setObjectName(QStringLiteral("packageGroupsApply"));
	m_cancel = new PackageActionButton(tr("Cancel Operation")); m_cancel->setObjectName(QStringLiteral("packageGroupsCancel"));
	for (auto* button : {m_apply, m_cancel}) { button->setAccessibleName(button->text()); button->setAutoDefault(false); layout->addWidget(button); }
	m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setAccessibleName(tr("WAD group progress")); layout->addWidget(m_progress);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); buttons->button(QDialogButtonBox::Close)->setAccessibleName(tr("Close WAD group review")); outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &PackageWadGroupsDialog::reject);
	connect(m_cancel, &QPushButton::clicked, this, [this]() { *m_cancelled = true; m_status->setText(tr("Stopping the group operation…")); updateControls(); });
	connect(m_prepare, &QPushButton::clicked, this, &PackageWadGroupsDialog::reviewEdit);
	connect(m_groups, &QComboBox::currentIndexChanged, this, [this](int index) {
		invalidate(); if (index < 0 || index >= m_inventory.groups.size()) { return; } const auto& group = m_inventory.groups.at(index);
		m_groups->setToolTip(tr("Group: %1\nFirst planned entry: %2\nMembers: %3\n%4").arg(group.name,
			locale().toString(static_cast<qlonglong>(group.ranges.first().first + 1)), locale().toString(static_cast<qlonglong>(group.memberCount)), group.error));
		m_target->setText(group.kind == QStringLiteral("gl") ? group.name.mid(3) : group.name);
		m_operation->setCurrentIndex(group.canRename ? 0 : 1); m_details->setPlainText(tr("Group: %1\nID: %2\nMembers including companions: %3\n%4")
			.arg(group.name, group.id, locale().toString(static_cast<qlonglong>(group.memberCount)), group.error)); updateControls();
	});
	connect(m_operation, &QComboBox::currentIndexChanged, this, &PackageWadGroupsDialog::invalidate);
	connect(m_target, &QLineEdit::textChanged, this, &PackageWadGroupsDialog::invalidate);
	connect(m_page, &QSpinBox::valueChanged, this, &PackageWadGroupsDialog::showPage);
	connect(m_changes, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
		const qsizetype at = (m_page->value() - 1) * 500 + row; if (row < 0 || at < 0 || at >= m_review.changes.size()) { return; }
		const auto& change = m_review.changes.at(at); m_details->setPlainText(tr("Group: %1\nSnapshot index: %2\nBefore: %3\nAfter: %4")
			.arg(m_review.groupName, locale().toString(static_cast<qlonglong>(change.entryIndex)), change.before, m_review.remove ? tr("Deleted") : change.after)
			+ QLatin1Char('\n') + m_review.warnings.join(QLatin1Char('\n')));
	});
	connect(m_apply, &QPushButton::clicked, this, [this]() {
		if (!m_ready || busy() || !apply) { return; } QString error;
		// Pass a copy so a rejected adoption can never consume the reviewed state.
		auto candidate = m_candidate;
		if (!apply(std::move(candidate), m_review, &error)) { m_ready = false; m_status->setText(error); updateControls(); return; }
		accept();
	});
	resize(QSize(900, 760).boundedTo(screen()->availableGeometry().size() - QSize(40, 40))); updateControls();
	QTimer::singleShot(0, this, &PackageWadGroupsDialog::inspect);
}
PackageWadGroupsDialog::~PackageWadGroupsDialog()
{
	*m_cancelled = true; if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool PackageWadGroupsDialog::busy() const { return m_thread != nullptr; }
void PackageWadGroupsDialog::reject()
{
	if (busy()) { m_closeRequested = true; *m_cancelled = true; m_status->setText(tr("Stopping the group operation…")); updateControls(); return; } QDialog::reject();
}
void PackageWadGroupsDialog::start(std::function<void()> work, std::function<void()> complete)
{
	if (busy()) { return; } *m_cancelled = false; m_thread = QThread::create(std::move(work));
	setProperty("operationRunning", true); m_progress->show(); updateControls();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; setProperty("operationRunning", false); m_progress->hide();
		if (m_closeRequested) { QDialog::reject(); return; }
		if (*m_cancelled) { m_ready = false; m_status->setText(tr("Group operation cancelled. No edits were applied.")); }
		else { complete(); } updateControls();
	}); m_thread->start();
}
void PackageWadGroupsDialog::inspect()
{
	auto result = std::make_shared<PackageWadGroupInventory>(); const auto cancelled = m_cancelled;
	start([model = m_original, cancelled, result]() { PackageReadControl control; control.isCancelled = [cancelled]() { return cancelled->load(); }; *result = inspectPackageWadGroups(model, control); },
		[this, result]() {
			m_inventory = std::move(*result); if (!m_inventory.succeeded()) { m_status->setText(m_inventory.error); return; }
			for (const auto& group : m_inventory.groups) { m_groups->addItem(group.kind == QStringLiteral("texture-tables") ? group.name
				: tr("%1 · %2").arg(group.name, locale().toString(static_cast<qlonglong>(group.ranges.first().first + 1)))); }
			m_status->setText(m_inventory.groups.isEmpty() ? tr("No semantic WAD groups were found.") : tr("Choose a group and review its proposed edit."));
		});
}
void PackageWadGroupsDialog::invalidate()
{
	m_ready = false; m_review = {}; m_changes->setRowCount(0); m_page->setRange(1, 1); m_page->setSuffix({}); m_details->clear();
	m_status->setText(tr("Review the current selection before applying changes.")); updateControls();
}
void PackageWadGroupsDialog::updateControls()
{
	const int at = m_groups->currentIndex(); const bool selected = at >= 0 && at < m_inventory.groups.size(); const bool removing = m_operation->currentIndex() == 1;
	m_groups->setEnabled(!busy()); m_operation->setEnabled(!busy() && selected);
	m_target->setEnabled(!busy() && selected && !removing && m_inventory.groups.at(at).canRename);
	m_prepare->setEnabled(!busy() && selected && m_inventory.groups.at(at).error.isEmpty() && (removing || (m_inventory.groups.at(at).canRename && !m_target->text().isEmpty())));
	if (!busy() && selected && !removing && !m_inventory.groups.at(at).canRename) { m_status->setText(tr("Namespace and texture-table groups support deletion only. Their marker names define their meaning.")); }
	m_apply->setEnabled(!busy() && m_ready && static_cast<bool>(apply)); m_cancel->setEnabled(busy() && !m_cancelled->load());
}
void PackageWadGroupsDialog::reviewEdit()
{
	const int at = m_groups->currentIndex(); if (busy() || at < 0 || at >= m_inventory.groups.size()) { return; }
	invalidate(); const bool removing = m_operation->currentIndex() == 1;
	const PackageWadGroupEditRequest request{m_inventory.groups.at(at).id, m_inventory.fingerprint, removing ? QString() : m_target->text(), removing};
	struct Result { PackageStagingModel model; PackageWadGroupEditReview review; QString error; bool ready = false; };
	auto result = std::make_shared<Result>(); result->model = m_original; const auto cancelled = m_cancelled; m_status->setText(tr("Preparing and verifying group changes…"));
	start([result, request, cancelled]() { PackageReadControl control; control.isCancelled = [cancelled]() { return cancelled->load(); };
		result->ready = stagePackageWadGroupEdit(&result->model, request, &result->review, &result->error, control); }, [this, result]() {
			m_ready = result->ready && !result->review.changes.isEmpty();
			if (!result->ready) { m_status->setText(result->error); return; }
			m_candidate = std::move(result->model); m_review = std::move(result->review);
			m_page->setRange(1, qMax(1, static_cast<int>((m_review.changes.size() + 499) / 500)));
			m_page->setSuffix(tr(" of %1").arg(m_page->maximum())); showPage();
			m_status->setText(m_ready ? tr("Reviewed changes: %1. Apply stages them as one undoable edit.").arg(locale().toString(static_cast<qlonglong>(m_review.changes.size()))) : tr("This edit makes no changes."));
		});
}
void PackageWadGroupsDialog::showPage()
{
	m_changes->setCurrentCell(-1, -1); const qsizetype first = (m_page->value() - 1) * 500;
	const int count = static_cast<int>(qBound(qsizetype{0}, m_review.changes.size() - first, qsizetype{500})); m_changes->setRowCount(count);
	for (int row = 0; row < count; ++row) {
		const auto& change = m_review.changes.at(first + row); const auto source = change.sourceOrdinal >= 0 ? locale().toString(change.sourceOrdinal + 1) : tr("New entry");
		const QStringList cells{source, change.before, m_review.remove ? tr("Deleted") : change.after};
		for (int column = 0; column < cells.size(); ++column) {
			auto* item = new QTableWidgetItem(QChar(0x2068) + cells.at(column) + QChar(0x2069)); item->setToolTip(cells.at(column));
			item->setData(Qt::AccessibleTextRole, tr("Source entry %1: %2 → %3").arg(cells.at(0), cells.at(1), cells.at(2))); m_changes->setItem(row, column, item);
		}
	}
	if (count) { m_changes->setCurrentCell(0, 0); m_changes->scrollToTop(); }
}
} // namespace vibestudio
