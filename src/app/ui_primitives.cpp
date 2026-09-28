#include "app/ui_primitives.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
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

// Same conservative glyph set the charts use, so a state reads the same in a
// chip, a chart, and a list row without depending on colour.
QString stateGlyph(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return QString(QChar(0x00b7));
	case OperationState::Queued:
		return QString(QChar(0x00bb));
	case OperationState::Loading:
		return QString(QChar(0x25cb));
	case OperationState::Running:
		return QString(QChar(0x25b6));
	case OperationState::Warning:
		return QString(QChar(0x25b2));
	case OperationState::Failed:
		return QString(QChar(0x00d7));
	case OperationState::Cancelled:
		return QStringLiteral("||");
	case OperationState::Completed:
		return QString(QChar(0x2713));
	}
	return QString(QChar(0x00b7));
}

void repolish(QWidget* widget)
{
	if (widget && widget->style()) {
		widget->style()->unpolish(widget);
		widget->style()->polish(widget);
	}
}

} // namespace

void applyMonospaceContentFont(QTextEdit* content)
{
	if (!content) {
		return;
	}
	const QFont font = studioMonospaceFont();
	content->setFont(font);
	content->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4.0);
}

QFont studioMonospaceFont()
{
	static const QStringList preferred = {
		QStringLiteral("Cascadia Mono"),
		QStringLiteral("Consolas"),
		QStringLiteral("JetBrains Mono"),
		QStringLiteral("SF Mono"),
		QStringLiteral("Menlo"),
		QStringLiteral("DejaVu Sans Mono"),
		QStringLiteral("Liberation Mono"),
		QStringLiteral("Noto Sans Mono"),
	};
	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	for (const QString& family : preferred) {
		if (QFontDatabase::hasFamily(family)) {
			font = QFont(family);
			break;
		}
	}
	font.setStyleHint(QFont::Monospace);
	font.setFixedPitch(true);
	const qreal base = QApplication::font().pointSizeF();
	if (base > 0) {
		font.setPointSizeF(base);
	}
	return font;
}

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
		{
			QStringLiteral("status-chip"),
			uiText("Status chip"),
			uiText("Compact non-color-only status indicator for project, package, compiler, installation, AI, validation, setup, and localization state."),
			{
				uiText("project health"),
				uiText("package staging"),
				uiText("compiler readiness"),
				uiText("AI-free mode"),
			},
		},
		{
			QStringLiteral("shortcut-registry"),
			uiText("Shortcut registry"),
			uiText("Shared keyboard shortcut metadata for command surfaces, conflict checks, accessible labels, and future user remapping."),
			{
				uiText("global commands"),
				uiText("package commands"),
				uiText("compiler commands"),
				uiText("activity commands"),
			},
		},
		{
			QStringLiteral("command-palette"),
			uiText("Command palette shell"),
			uiText("Searchable command metadata shell with categories, summaries, shortcut hints, and staged/destructive flags."),
			{
				uiText("workspace commands"),
				uiText("support commands"),
				uiText("QA commands"),
				uiText("AI review commands"),
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

	// One compact status row: state chip, title, detail, and a progress bar
	// that only appears while work is queued or running. Skeleton rows appear
	// beneath it for the same busy states.
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(12, 8, 12, 8);
	root->setSpacing(6);

	auto* header = new QHBoxLayout;
	header->setSpacing(10);
	m_stateLabel = new QLabel;
	m_stateLabel->setObjectName("statusChip");
	m_stateLabel->setAccessibleName(uiText("Loading pane state"));
	m_stateLabel->setAlignment(Qt::AlignCenter);
	header->addWidget(m_stateLabel, 0, Qt::AlignTop);

	auto* text = new QVBoxLayout;
	text->setSpacing(1);
	m_titleLabel = new QLabel;
	m_titleLabel->setObjectName("loadingTitle");
	m_titleLabel->setAccessibleName(uiText("Loading pane title"));
	m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_titleLabel->setWordWrap(true);
	text->addWidget(m_titleLabel);

	m_detailLabel = new QLabel;
	m_detailLabel->setObjectName("loadingDetail");
	m_detailLabel->setAccessibleName(uiText("Loading pane detail"));
	m_detailLabel->setWordWrap(true);
	m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	text->addWidget(m_detailLabel);
	header->addLayout(text, 1);

	m_progress = new QProgressBar;
	m_progress->setObjectName("loadingProgress");
	m_progress->setAccessibleName(uiText("Loading pane progress"));
	m_progress->setTextVisible(true);
	m_progress->setFixedWidth(190);
	header->addWidget(m_progress, 0, Qt::AlignVCenter);
	root->addLayout(header);

	m_placeholderHost = new QWidget;
	m_placeholderHost->setObjectName("skeletonHost");
	m_placeholderHost->setAccessibleName(uiText("Loading placeholders"));
	m_placeholderHost->setAccessibleDescription(uiText("Context-specific placeholder rows for loading or pending pane content."));
	m_placeholderLayout = new QVBoxLayout(m_placeholderHost);
	m_placeholderLayout->setContentsMargins(0, 0, 0, 0);
	m_placeholderLayout->setSpacing(6);
	m_placeholderLayout->addStretch(1);
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
	m_stateLabel->setText(QStringLiteral("%1 %2").arg(stateGlyph(m_state), stateName));
	m_stateLabel->setToolTip(stateName);
	const QString stateId = operationStateId(m_state);
	if (m_stateLabel->property("operationState").toString() != stateId) {
		m_stateLabel->setProperty("operationState", stateId);
		repolish(m_stateLabel);
	}

	m_detailLabel->setText(m_detail.isEmpty() ? uiText("Preparing content.") : m_detail);

	const bool busy = isBusyState(m_state);
	if (m_progressValue.total > 0) {
		m_progress->setRange(0, m_progressValue.total);
		m_progress->setValue(m_progressValue.current);
		m_progress->setFormat(uiText("%1%").arg(operationProgressPercent(m_progressValue)));
	} else if (busy && !m_reducedMotion) {
		m_progress->setRange(0, 0);
		m_progress->setFormat(stateName);
	} else {
		m_progress->setRange(0, 1);
		m_progress->setValue(operationStateIsTerminal(m_state) ? 1 : 0);
		m_progress->setFormat(stateName);
	}
	// A finished or idle pane has nothing to measure, so the bar only takes
	// space while work is queued or running.
	m_progress->setVisible(busy);
	m_placeholderHost->setVisible(busy);
}

void LoadingPane::rebuildPlaceholders()
{
	if (!m_placeholderLayout) {
		return;
	}

	const QStringList rows = m_placeholderRows.isEmpty() ? defaultPlaceholderRows() : m_placeholderRows;
	while (m_placeholderLabels.size() < rows.size()) {
		auto* label = new QLabel(m_placeholderHost);
		label->setObjectName("skeletonRow");
		label->setMinimumHeight(24);
		m_placeholderLayout->insertWidget(m_placeholderLabels.size(), label);
		m_placeholderLabels.push_back(label);
	}

	for (int index = 0; index < m_placeholderLabels.size(); ++index) {
		QLabel* label = m_placeholderLabels[index];
		const bool active = index < rows.size();
		if (active) {
			const QString& row = rows[index];
			label->setText(uiText("%1 loading placeholder").arg(row));
			label->setAccessibleName(uiText("%1 placeholder").arg(row));
		}
		label->setVisible(active);
	}
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
	header->setSpacing(6);
	auto* titleStack = new QVBoxLayout;
	titleStack->setSpacing(1);
	m_titleLabel = new QLabel;
	m_titleLabel->setObjectName("drawerTitle");
	m_titleLabel->setAccessibleName(uiText("Detail drawer title"));
	m_titleLabel->setWordWrap(true);
	m_subtitleLabel = new QLabel;
	m_subtitleLabel->setObjectName("drawerSubtitle");
	m_subtitleLabel->setAccessibleName(uiText("Detail drawer subtitle"));
	m_subtitleLabel->setWordWrap(true);
	titleStack->addWidget(m_titleLabel);
	titleStack->addWidget(m_subtitleLabel);
	header->addLayout(titleStack, 1);

	m_copyButton = new QPushButton(uiText("Copy"));
	m_copyButton->setAccessibleName(uiText("Copy selected detail text"));
	m_copyButton->setToolTip(uiText("Copy the selected section to the clipboard."));
	m_copyButton->setProperty("variant", QStringLiteral("ghost"));
	connect(m_copyButton, &QPushButton::clicked, this, [this]() {
		copyCurrentSection();
	});
	header->addWidget(m_copyButton, 0, Qt::AlignTop);

	m_toggleButton = new QPushButton(uiText("Hide Details"));
	m_toggleButton->setAccessibleName(uiText("Show or hide detail drawer"));
	m_toggleButton->setProperty("variant", QStringLiteral("ghost"));
	connect(m_toggleButton, &QPushButton::clicked, this, [this]() {
		setExpanded(!m_expanded);
	});
	header->addWidget(m_toggleButton, 0, Qt::AlignTop);
	root->addLayout(header);

	m_body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(m_body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(8);

	m_emptyLabel = new QLabel(uiText("No details yet."));
	m_emptyLabel->setObjectName("drawerEmpty");
	m_emptyLabel->setAccessibleName(uiText("Detail drawer empty state"));
	m_emptyLabel->setWordWrap(true);
	m_emptyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	bodyLayout->addWidget(m_emptyLabel);

	m_sectionsList = new QListWidget;
	m_sectionsList->setObjectName("detailSections");
	m_sectionsList->setAccessibleName(uiText("Detail sections"));
	m_sectionsList->setAccessibleDescription(uiText("Available summary, log, metadata, manifest, and raw diagnostic sections."));
	m_sectionsList->setMaximumHeight(132);
	m_sectionsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_sectionsList->setTextElideMode(Qt::ElideRight);
	connect(m_sectionsList, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshCurrentSection();
	});
	bodyLayout->addWidget(m_sectionsList);

	m_content = new QTextEdit;
	m_content->setObjectName("detailContent");
	m_content->setAccessibleName(uiText("Detail content"));
	m_content->setAccessibleDescription(uiText("Selected detail content ready for copying into support notes or diagnostics."));
	m_content->setReadOnly(true);
	m_content->setLineWrapMode(QTextEdit::WidgetWidth);
	// Logs, manifests, and command lines line up in a fixed-pitch font, with
	// tabs four columns wide the way source files are usually written.
	applyMonospaceContentFont(m_content);
	bodyLayout->addWidget(m_content, 1);

	root->addWidget(m_body, 1);

	setTitle(uiText("Details"));
	setSubtitle(uiText("Select a section for deeper context."));
	setSections({});
}

void DetailDrawer::changeEvent(QEvent* event)
{
	// An explicitly set font does not follow QApplication::setFont(), so the
	// fixed-pitch content re-derives its size when the text scale changes.
	if ((event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) && m_content) {
		applyMonospaceContentFont(m_content);
	}
	QFrame::changeEvent(event);
}

void DetailDrawer::setEmbedded(bool embedded)
{
	setProperty("embedded", embedded);
	// Inside a tab the drawer is the whole panel, so hiding it would leave an
	// empty tab; only the copy action stays in its header.
	if (m_toggleButton) {
		m_toggleButton->setVisible(!embedded);
	}
	if (layout()) {
		layout()->setContentsMargins(embedded ? 0 : 12, embedded ? 4 : 10, embedded ? 0 : 12, embedded ? 0 : 10);
	}
	repolish(this);
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
	const bool empty = m_sections.isEmpty();
	// With nothing to show, one quiet line replaces the section list and the
	// content box instead of two empty frames.
	if (m_emptyLabel) {
		m_emptyLabel->setVisible(empty);
	}
	m_content->setVisible(!empty);
	if (empty) {
		m_sectionsList->setVisible(false);
		m_copyButton->setEnabled(false);
		return;
	}
	// A single section needs no chooser.
	m_sectionsList->setVisible(m_sections.size() > 1);

	for (const DetailSection& section : m_sections) {
		const QString label = section.summary.isEmpty()
			? QStringLiteral("%1 %2").arg(stateGlyph(section.state), section.title)
			: QStringLiteral("%1 %2  %3  %4").arg(stateGlyph(section.state), section.title, QString(QChar(0x2014)), section.summary);
		auto* item = new QListWidgetItem(label);
		item->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(section.title, localizedStateName(section.state), section.summary));
		item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2. %3").arg(section.title, localizedStateName(section.state), section.summary));
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
