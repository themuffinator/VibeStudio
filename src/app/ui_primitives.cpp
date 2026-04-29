#include "app/ui_primitives.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {

namespace {

QString uiText(const char* source)
{
	return QCoreApplication::translate("VibeStudioUiPrimitives", source);
}

QString normalizedSectionId(const QString& value)
{
	QString normalized = value.trimmed().toLower();
	normalized.replace('_', '-');
	normalized.replace(' ', '-');
	return normalized;
}

QString localizedStateName(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return uiText("Idle");
	case OperationState::Queued:
		return uiText("Queued");
	case OperationState::Loading:
		return uiText("Loading");
	case OperationState::Running:
		return uiText("Running");
	case OperationState::Warning:
		return uiText("Warning");
	case OperationState::Failed:
		return uiText("Failed");
	case OperationState::Cancelled:
		return uiText("Cancelled");
	case OperationState::Completed:
		return uiText("Completed");
	}
	return uiText("Idle");
}

QStringList defaultPlaceholderRows()
{
	return {
		uiText("Summary"),
		uiText("Metadata"),
		uiText("Diagnostics"),
	};
}

bool isBusyState(OperationState state)
{
	return state == OperationState::Queued || state == OperationState::Loading || state == OperationState::Running;
}

} // namespace

QVector<UiPrimitiveDescriptor> uiPrimitiveDescriptors()
{
	return {
		{
			QStringLiteral("loading-pane"),
			uiText("Loading pane"),
			uiText("Reusable pane and preview loading surface with state text, progress, reduced-motion behavior, and context-specific skeleton rows."),
			{
				uiText("package loading"),
				uiText("preview generation"),
				uiText("compiler stages"),
				uiText("AI requests"),
			},
		},
		{
			QStringLiteral("detail-drawer"),
			uiText("Detail drawer"),
			uiText("Reusable collapsible drawer for summary-first logs, metadata, manifests, raw diagnostics, and support-copy text."),
			{
				uiText("task logs"),
				uiText("package metadata"),
				uiText("compiler manifests"),
				uiText("raw diagnostics"),
			},
		},
	};
}

LoadingPane::LoadingPane(QWidget* parent)
	: QFrame(parent)
{
	setObjectName("loadingPane");
	setAccessibleName(uiText("Loading pane"));
	setAccessibleDescription(uiText("Shows operation state, progress, and placeholder rows while pane content is loading."));

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	auto* header = new QHBoxLayout;
	m_titleLabel = new QLabel;
	m_titleLabel->setObjectName("loadingTitle");
	m_titleLabel->setAccessibleName(uiText("Loading pane title"));
	m_stateLabel = new QLabel;
	m_stateLabel->setObjectName("statusChip");
	m_stateLabel->setAccessibleName(uiText("Loading pane state"));
	m_stateLabel->setAlignment(Qt::AlignCenter);
	header->addWidget(m_titleLabel, 1);
	header->addWidget(m_stateLabel, 0, Qt::AlignRight);
	root->addLayout(header);

	m_detailLabel = new QLabel;
	m_detailLabel->setObjectName("loadingDetail");
	m_detailLabel->setAccessibleName(uiText("Loading pane detail"));
	m_detailLabel->setWordWrap(true);
	root->addWidget(m_detailLabel);

	m_progress = new QProgressBar;
	m_progress->setObjectName("loadingProgress");
	m_progress->setAccessibleName(uiText("Loading pane progress"));
	m_progress->setTextVisible(true);
	root->addWidget(m_progress);

	m_placeholderHost = new QWidget;
	m_placeholderHost->setObjectName("skeletonHost");
	m_placeholderHost->setAccessibleName(uiText("Loading placeholders"));
	m_placeholderHost->setAccessibleDescription(uiText("Context-specific placeholder rows for loading or pending pane content."));
	m_placeholderLayout = new QVBoxLayout(m_placeholderHost);
	m_placeholderLayout->setContentsMargins(0, 0, 0, 0);
	m_placeholderLayout->setSpacing(6);
	root->addWidget(m_placeholderHost);

	setTitle(uiText("Loading"));
	setDetail(uiText("Preparing content."));
	setPlaceholderRows(defaultPlaceholderRows());
	refresh();
}

void LoadingPane::setTitle(const QString& title)
{
	m_title = title.trimmed();
	refresh();
}

void LoadingPane::setDetail(const QString& detail)
{
	m_detail = detail.trimmed();
	refresh();
}

void LoadingPane::setState(OperationState state, const QString& statusText)
{
	m_state = state;
	m_statusText = statusText.trimmed();
	refresh();
}

void LoadingPane::setProgress(OperationProgress progress)
{
	m_progressValue.total = std::max(0, progress.total);
	m_progressValue.current = std::clamp(progress.current, 0, m_progressValue.total);
	refresh();
}

void LoadingPane::setPlaceholderRows(const QStringList& rows)
{
	m_placeholderRows.clear();
	for (const QString& row : rows) {
		const QString normalized = row.trimmed();
		if (!normalized.isEmpty()) {
			m_placeholderRows.push_back(normalized);
		}
	}
	rebuildPlaceholders();
	refresh();
}

void LoadingPane::setReducedMotion(bool reducedMotion)
{
	m_reducedMotion = reducedMotion;
	refresh();
}

QString LoadingPane::title() const
{
	return m_title;
}

QString LoadingPane::detail() const
{
	return m_detail;
}

OperationState LoadingPane::state() const
{
	return m_state;
}

OperationProgress LoadingPane::progress() const
{
	return m_progressValue;
}

QStringList LoadingPane::placeholderRows() const
{
	return m_placeholderRows;
}

bool LoadingPane::reducedMotion() const
{
	return m_reducedMotion;
}

void LoadingPane::refresh()
{
	if (!m_titleLabel || !m_progress) {
		return;
	}

	const QString stateName = m_statusText.isEmpty() ? localizedStateName(m_state) : m_statusText;
	const QString titleText = m_title.isEmpty() ? uiText("Loading") : m_title;
	m_titleLabel->setText(titleText);
	m_stateLabel->setText(stateName);
	m_stateLabel->setProperty("operationState", operationStateId(m_state));
	m_stateLabel->style()->unpolish(m_stateLabel);
	m_stateLabel->style()->polish(m_stateLabel);

	m_detailLabel->setText(m_detail.isEmpty() ? uiText("Preparing content.") : m_detail);

	if (m_progressValue.total > 0) {
		m_progress->setRange(0, m_progressValue.total);
		m_progress->setValue(m_progressValue.current);
		m_progress->setFormat(uiText("%1%").arg(operationProgressPercent(m_progressValue)));
	} else if (isBusyState(m_state) && !m_reducedMotion) {
		m_progress->setRange(0, 0);
		m_progress->setFormat(stateName);
	} else {
		m_progress->setRange(0, 1);
		m_progress->setValue(operationStateIsTerminal(m_state) ? 1 : 0);
		m_progress->setFormat(stateName);
	}

	m_placeholderHost->setVisible(isBusyState(m_state));
}

void LoadingPane::rebuildPlaceholders()
{
	if (!m_placeholderLayout) {
		return;
	}

	while (QLayoutItem* item = m_placeholderLayout->takeAt(0)) {
		delete item->widget();
		delete item;
	}

	const QStringList rows = m_placeholderRows.isEmpty() ? defaultPlaceholderRows() : m_placeholderRows;
	for (const QString& row : rows) {
		auto* label = new QLabel(uiText("%1 loading placeholder").arg(row));
		label->setObjectName("skeletonRow");
		label->setAccessibleName(uiText("%1 placeholder").arg(row));
		label->setMinimumHeight(24);
		m_placeholderLayout->addWidget(label);
	}
	m_placeholderLayout->addStretch(1);
}

DetailDrawer::DetailDrawer(QWidget* parent)
	: QFrame(parent)
{
	setObjectName("detailDrawer");
	setAccessibleName(uiText("Detail drawer"));
	setAccessibleDescription(uiText("Collapsible details for logs, metadata, manifests, and raw diagnostics."));

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	auto* header = new QHBoxLayout;
	auto* titleStack = new QVBoxLayout;
	m_titleLabel = new QLabel;
	m_titleLabel->setObjectName("drawerTitle");
	m_titleLabel->setAccessibleName(uiText("Detail drawer title"));
	m_subtitleLabel = new QLabel;
	m_subtitleLabel->setObjectName("drawerSubtitle");
	m_subtitleLabel->setAccessibleName(uiText("Detail drawer subtitle"));
	m_subtitleLabel->setWordWrap(true);
	titleStack->addWidget(m_titleLabel);
	titleStack->addWidget(m_subtitleLabel);
	header->addLayout(titleStack, 1);

	m_toggleButton = new QPushButton(uiText("Hide Details"));
	m_toggleButton->setAccessibleName(uiText("Show or hide detail drawer"));
	connect(m_toggleButton, &QPushButton::clicked, this, [this]() {
		setExpanded(!m_expanded);
	});
	header->addWidget(m_toggleButton);

	m_copyButton = new QPushButton(uiText("Copy"));
	m_copyButton->setAccessibleName(uiText("Copy selected detail text"));
	connect(m_copyButton, &QPushButton::clicked, this, [this]() {
		copyCurrentSection();
	});
	header->addWidget(m_copyButton);
	root->addLayout(header);

	m_body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(m_body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(8);

	m_sectionsList = new QListWidget;
	m_sectionsList->setObjectName("detailSections");
	m_sectionsList->setAccessibleName(uiText("Detail sections"));
	m_sectionsList->setAccessibleDescription(uiText("Available summary, log, metadata, manifest, and raw diagnostic sections."));
	m_sectionsList->setMaximumHeight(116);
	connect(m_sectionsList, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshCurrentSection();
	});
	bodyLayout->addWidget(m_sectionsList);

	m_content = new QTextEdit;
	m_content->setObjectName("detailContent");
	m_content->setAccessibleName(uiText("Detail content"));
	m_content->setAccessibleDescription(uiText("Selected detail content ready for copying into support notes or diagnostics."));
	m_content->setReadOnly(true);
	bodyLayout->addWidget(m_content, 1);

	root->addWidget(m_body, 1);

	setTitle(uiText("Details"));
	setSubtitle(uiText("Select a section for deeper context."));
	setSections({});
}

void DetailDrawer::setTitle(const QString& title)
{
	m_title = title.trimmed();
	refreshHeader();
}

void DetailDrawer::setSubtitle(const QString& subtitle)
{
	m_subtitle = subtitle.trimmed();
	refreshHeader();
}

void DetailDrawer::setSections(const QVector<DetailSection>& sections)
{
	m_sections.clear();
	for (DetailSection section : sections) {
		section.id = normalizedSectionId(section.id.isEmpty() ? section.title : section.id);
		section.title = section.title.trimmed();
		section.summary = section.summary.trimmed();
		section.content = section.content.trimmed();
		if (!section.id.isEmpty() && !section.title.isEmpty()) {
			m_sections.push_back(section);
		}
	}
	if (!findSection(m_currentSectionId)) {
		m_currentSectionId = m_sections.isEmpty() ? QString() : m_sections.front().id;
	}
	refreshSections();
	refreshCurrentSection();
}

void DetailDrawer::showSection(const QString& sectionId)
{
	const QString normalized = normalizedSectionId(sectionId);
	if (!findSection(normalized)) {
		return;
	}
	m_currentSectionId = normalized;
	refreshSections();
	refreshCurrentSection();
}

void DetailDrawer::setExpanded(bool expanded)
{
	m_expanded = expanded;
	if (m_body) {
		m_body->setVisible(m_expanded);
	}
	if (m_toggleButton) {
		m_toggleButton->setText(m_expanded ? uiText("Hide Details") : uiText("Show Details"));
	}
}

QString DetailDrawer::title() const
{
	return m_title;
}

QString DetailDrawer::subtitle() const
{
	return m_subtitle;
}

QVector<DetailSection> DetailDrawer::sections() const
{
	return m_sections;
}

QString DetailDrawer::currentSectionId() const
{
	return m_currentSectionId;
}

QString DetailDrawer::currentSectionText() const
{
	const DetailSection* section = findSection(m_currentSectionId);
	return section ? section->content : QString();
}

bool DetailDrawer::isExpanded() const
{
	return m_expanded;
}

void DetailDrawer::refreshHeader()
{
	if (!m_titleLabel) {
		return;
	}
	m_titleLabel->setText(m_title.isEmpty() ? uiText("Details") : m_title);
	m_subtitleLabel->setText(m_subtitle.isEmpty() ? uiText("Select a section for deeper context.") : m_subtitle);
}

void DetailDrawer::refreshSections()
{
	if (!m_sectionsList) {
		return;
	}

	QSignalBlocker blocker(m_sectionsList);
	m_sectionsList->clear();
	if (m_sections.isEmpty()) {
		auto* item = new QListWidgetItem(uiText("No details available"));
		item->setFlags(Qt::NoItemFlags);
		m_sectionsList->addItem(item);
		m_copyButton->setEnabled(false);
		return;
	}

	for (const DetailSection& section : m_sections) {
		const QString label = section.summary.isEmpty()
			? QStringLiteral("%1 [%2]").arg(section.title, localizedStateName(section.state))
			: QStringLiteral("%1 [%2]\n%3").arg(section.title, localizedStateName(section.state), section.summary);
		auto* item = new QListWidgetItem(label);
		item->setData(Qt::UserRole, section.id);
		item->setData(Qt::UserRole + 1, operationStateId(section.state));
		m_sectionsList->addItem(item);
		if (section.id == m_currentSectionId) {
			m_sectionsList->setCurrentItem(item);
		}
	}
	if (!m_sectionsList->currentItem()) {
		m_sectionsList->setCurrentRow(0);
	}
	m_copyButton->setEnabled(true);
}

void DetailDrawer::refreshCurrentSection()
{
	if (!m_content) {
		return;
	}

	const QListWidgetItem* item = m_sectionsList ? m_sectionsList->currentItem() : nullptr;
	if (item && !item->data(Qt::UserRole).toString().isEmpty()) {
		m_currentSectionId = item->data(Qt::UserRole).toString();
	}

	const DetailSection* section = findSection(m_currentSectionId);
	if (!section) {
		m_content->setPlainText(uiText("No details available."));
		return;
	}
	m_content->setPlainText(section->content.isEmpty() ? uiText("No details available.") : section->content);
}

void DetailDrawer::copyCurrentSection()
{
	const QString text = currentSectionText();
	if (text.isEmpty()) {
		return;
	}
	if (QClipboard* clipboard = QGuiApplication::clipboard()) {
		clipboard->setText(text);
	}
}

const DetailSection* DetailDrawer::findSection(const QString& sectionId) const
{
	const QString normalized = normalizedSectionId(sectionId);
	const auto match = std::find_if(m_sections.cbegin(), m_sections.cend(), [&normalized](const DetailSection& section) {
		return section.id == normalized;
	});
	return match == m_sections.cend() ? nullptr : &(*match);
}

} // namespace vibestudio
