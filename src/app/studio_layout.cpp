#include "app/studio_layout.h"

#include "app/studio_charts.h"
#include "app/studio_icons.h"
#include "app/studio_theme.h"
#include "core/operation_state.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace vibestudio {

namespace {

QString layoutText(const char* source)
{
	return QCoreApplication::translate("VibeStudioLayout", source);
}

QString withoutMnemonic(QString text)
{
	text.remove(QLatin1Char('&'));
	text.remove(QChar(0x2026));
	return text.trimmed();
}

// Reports a narrow preferred width so a list lays its rows out at the
// viewport width; the style then elides each line that does not fit.
class ElidingItemDelegate final : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QSize hint = QStyledItemDelegate::sizeHint(option, index);
		// Tiles fill their grid cell, so every tile and its selection is the
		// same width and a name gets the whole cell before it elides.
		const auto* list = qobject_cast<const QListView*>(parent());
		if (list && list->viewMode() == QListView::IconMode) {
			if (list->gridSize().isValid()) {
				hint.setWidth(list->gridSize().width());
			}
			return hint;
		}
		hint.setWidth(std::min(hint.width(), 96));
		return hint;
	}
};

constexpr int kRailExpandedWidth = 184;
constexpr int kRailCompactWidth = 52;

} // namespace

// ---------------------------------------------------------------------------
// ElidedLabel
// ---------------------------------------------------------------------------

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent)
	: QLabel(parent)
{
	setWordWrap(false);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	setText(text);
}

void ElidedLabel::setText(const QString& text)
{
	m_fullText = text;
	setToolTip(text);
	setAccessibleDescription(text);
	refreshElision();
	updateGeometry();
}

QString ElidedLabel::fullText() const
{
	return m_fullText;
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode)
{
	m_mode = mode;
	refreshElision();
}

QSize ElidedLabel::sizeHint() const
{
	const QMargins margins = contentsMargins();
	const int width = fontMetrics().horizontalAdvance(m_fullText) + margins.left() + margins.right() + 2 * frameWidth() + 4;
	return {width, QLabel::sizeHint().height()};
}

QSize ElidedLabel::minimumSizeHint() const
{
	return {fontMetrics().horizontalAdvance(QStringLiteral("...")) * 2, QLabel::minimumSizeHint().height()};
}

void ElidedLabel::resizeEvent(QResizeEvent* event)
{
	QLabel::resizeEvent(event);
	refreshElision();
}

void ElidedLabel::changeEvent(QEvent* event)
{
	QLabel::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		refreshElision();
	}
}

void ElidedLabel::refreshElision()
{
	const int available = std::max(0, contentsRect().width() - 2);
	QLabel::setText(fontMetrics().elidedText(m_fullText, m_mode, available));
}

// ---------------------------------------------------------------------------
// ModeRail
// ---------------------------------------------------------------------------

ModeRail::ModeRail(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("modeRail"));
	setAttribute(Qt::WA_StyledBackground, true);
	setAccessibleName(tr("Mode rail"));
	setAccessibleDescription(tr("Switches the studio between work surfaces. Use the arrow keys to move between modes."));

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 8, 0, 6);
	root->setSpacing(0);

	m_topLayout = new QVBoxLayout;
	m_topLayout->setContentsMargins(0, 0, 0, 0);
	m_topLayout->setSpacing(1);
	root->addLayout(m_topLayout);
	root->addStretch(1);

	m_bottomLayout = new QVBoxLayout;
	m_bottomLayout->setContentsMargins(0, 0, 0, 0);
	m_bottomLayout->setSpacing(1);
	root->addLayout(m_bottomLayout);

	root->addWidget(createDivider());

	auto* toggleRow = new QHBoxLayout;
	toggleRow->setContentsMargins(8, 2, 8, 0);
	m_toggle = new QToolButton;
	m_toggle->setObjectName(QStringLiteral("railToggle"));
	m_toggle->setIconSize(QSize(16, 16));
	m_toggle->setFocusPolicy(Qt::TabFocus);
	connect(m_toggle, &QToolButton::clicked, this, [this]() {
		setCompact(!m_compact);
		emit compactChanged(m_compact);
	});
	toggleRow->addWidget(m_toggle);
	toggleRow->addStretch(1);
	root->addLayout(toggleRow);

	m_group = new QButtonGroup(this);
	m_group->setExclusive(true);
	connect(m_group, &QButtonGroup::idClicked, this, [this](int id) {
		emit currentIdChanged(id);
	});

	refreshCompactPresentation();
}

void ModeRail::setEntries(const QVector<ModeRailEntry>& entries)
{
	m_entries = entries;
	rebuild();
}

void ModeRail::rebuild()
{
	for (QToolButton* button : std::as_const(m_buttons)) {
		m_group->removeButton(button);
		button->deleteLater();
	}
	m_buttons.clear();
	auto clearLayout = [](QVBoxLayout* layout) {
		while (QLayoutItem* item = layout->takeAt(0)) {
			if (QWidget* widget = item->widget()) {
				widget->deleteLater();
			}
			delete item;
		}
	};
	clearLayout(m_topLayout);
	clearLayout(m_bottomLayout);

	for (const ModeRailEntry& entry : std::as_const(m_entries)) {
		QVBoxLayout* target = entry.pinnedToBottom ? m_bottomLayout : m_topLayout;
		if (entry.startsGroup && target->count() > 0) {
			target->addWidget(createDivider());
		}
		auto* button = new QToolButton;
		button->setObjectName(QStringLiteral("modeButton"));
		button->setCheckable(true);
		button->setProperty("railIconName", entry.iconName);
		button->setText(entry.label);
		button->setToolTip(QStringLiteral("%1\n%2").arg(entry.label, entry.hint));
		button->setAccessibleName(entry.label);
		button->setAccessibleDescription(entry.hint);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		button->installEventFilter(this);
		m_group->addButton(button, entry.id);
		m_buttons.push_back(button);
		target->addWidget(button);
	}
	refreshCompactPresentation();
}

int ModeRail::count() const
{
	return m_entries.size();
}

int ModeRail::currentId() const
{
	return m_group ? m_group->checkedId() : -1;
}

void ModeRail::setCurrentId(int id)
{
	if (QAbstractButton* button = m_group ? m_group->button(id) : nullptr) {
		button->setChecked(true);
	}
	// Roving tab stop: only the current mode takes Tab focus, and the arrow
	// keys move between the rest. Clicking never takes focus, so the focus
	// ring appears only for keyboard users.
	for (QToolButton* candidate : std::as_const(m_buttons)) {
		candidate->setFocusPolicy(candidate->isChecked() ? Qt::TabFocus : Qt::NoFocus);
	}
}

QString ModeRail::label(int id) const
{
	for (const ModeRailEntry& entry : m_entries) {
		if (entry.id == id) {
			return entry.label;
		}
	}
	return {};
}

void ModeRail::setCompact(bool compact)
{
	if (m_compact == compact && !m_buttons.isEmpty()) {
		refreshCompactPresentation();
		return;
	}
	m_compact = compact;
	refreshCompactPresentation();
}

bool ModeRail::isCompact() const
{
	return m_compact;
}

void ModeRail::refreshCompactPresentation()
{
	const QSize glyph = scaledIconSize(QSize(20, 20));
	const QSize expandedIcon(glyph.width() + glyph.width() * 2 / 5, glyph.height());
	int widestLabel = 0;
	for (QToolButton* button : std::as_const(m_buttons)) {
		const QString iconName = button->property("railIconName").toString();
		// Expanded rows reserve a gap after the glyph; compact rows centre it.
		button->setIcon(studioIcon(iconName, StudioIconTone::Normal, m_compact ? StudioIconAlignment::Centre : StudioIconAlignment::Leading));
		button->setIconSize(m_compact ? glyph : expandedIcon);
		button->setToolButtonStyle(m_compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
		QFont bold = button->font();
		bold.setWeight(QFont::DemiBold);
		widestLabel = std::max(widestLabel, QFontMetrics(bold).horizontalAdvance(button->text()));
	}
	// Expanded width fits the longest (possibly translated) label at the
	// current text scale; compact width fits one glyph plus its padding.
	const int expandedWidth = std::max(kRailExpandedWidth, widestLabel + expandedIcon.width() + 44);
	const int compactWidth = std::max(kRailCompactWidth, glyph.width() + 32);
	setFixedWidth(m_compact ? compactWidth : expandedWidth);
	if (m_toggle) {
		m_toggle->setIconSize(scaledIconSize(QSize(16, 16)));
		m_toggle->setIcon(studioIcon(m_compact ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"), StudioIconTone::Muted));
		const QString text = m_compact ? tr("Expand navigation") : tr("Collapse navigation");
		m_toggle->setToolTip(text);
		m_toggle->setAccessibleName(text);
	}
}

void ModeRail::focusRelative(int step)
{
	if (m_buttons.isEmpty()) {
		return;
	}
	int index = 0;
	for (int candidate = 0; candidate < m_buttons.size(); ++candidate) {
		if (m_buttons[candidate]->hasFocus()) {
			index = candidate;
			break;
		}
	}
	int next = index + step;
	if (step == INT_MIN) {
		next = 0;
	} else if (step == INT_MAX) {
		next = m_buttons.size() - 1;
	}
	next = std::clamp(next, 0, static_cast<int>(m_buttons.size()) - 1);
	QToolButton* target = m_buttons[next];
	target->setFocus(Qt::TabFocusReason);
	if (!target->isChecked()) {
		target->click();
	}
}

bool ModeRail::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::KeyPress && m_buttons.contains(qobject_cast<QToolButton*>(watched))) {
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		switch (keyEvent->key()) {
		case Qt::Key_Up:
		case Qt::Key_Left:
			focusRelative(-1);
			return true;
		case Qt::Key_Down:
		case Qt::Key_Right:
			focusRelative(1);
			return true;
		case Qt::Key_Home:
			focusRelative(INT_MIN);
			return true;
		case Qt::Key_End:
			focusRelative(INT_MAX);
			return true;
		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void ModeRail::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
		update();
	}
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		refreshCompactPresentation();
	}
	QWidget::changeEvent(event);
}

// ---------------------------------------------------------------------------
// PageHeader
// ---------------------------------------------------------------------------

PageHeader::PageHeader(const QString& iconName, const QString& title, QWidget* parent)
	: QWidget(parent)
	, m_iconName(iconName)
{
	setObjectName(QStringLiteral("pageHeader"));
	setAttribute(Qt::WA_StyledBackground, true);
	setAccessibleName(layoutText("Page header"));

	auto* root = new QHBoxLayout(this);
	root->setContentsMargins(16, 10, 14, 10);
	root->setSpacing(10);

	m_icon = new QLabel;
	m_icon->setObjectName(QStringLiteral("pageIcon"));
	m_icon->setFixedSize(26, 26);
	m_icon->setAlignment(Qt::AlignCenter);
	m_icon->setAccessibleName(QString());
	root->addWidget(m_icon, 0, Qt::AlignVCenter);

	auto* titleStack = new QVBoxLayout;
	titleStack->setContentsMargins(0, 0, 0, 0);
	titleStack->setSpacing(0);
	m_title = new QLabel(title);
	m_title->setObjectName(QStringLiteral("pageTitle"));
	m_title->setAccessibleName(layoutText("Page title"));
	titleStack->addWidget(m_title);
	m_subtitle = new ElidedLabel;
	m_subtitle->setObjectName(QStringLiteral("pageSubtitle"));
	m_subtitle->setAccessibleName(layoutText("Page context"));
	m_subtitle->setElideMode(Qt::ElideMiddle);
	m_subtitle->setVisible(false);
	titleStack->addWidget(m_subtitle);
	root->addLayout(titleStack, 1);

	m_statusLayout = new QHBoxLayout;
	m_statusLayout->setContentsMargins(6, 0, 0, 0);
	m_statusLayout->setSpacing(6);
	root->addLayout(m_statusLayout);

	m_actionLayout = new QHBoxLayout;
	m_actionLayout->setContentsMargins(0, 0, 0, 0);
	m_actionLayout->setSpacing(6);
	root->addLayout(m_actionLayout);

	refreshIcon();
}

void PageHeader::setTitle(const QString& title)
{
	m_title->setText(title);
}

void PageHeader::setSubtitle(const QString& subtitle)
{
	m_subtitle->setText(subtitle);
	m_subtitle->setVisible(!subtitle.trimmed().isEmpty());
}

QLabel* PageHeader::titleLabel() const
{
	return m_title;
}

ElidedLabel* PageHeader::subtitleLabel() const
{
	return m_subtitle;
}

void PageHeader::addStatusWidget(QWidget* widget)
{
	m_statusLayout->addWidget(widget, 0, Qt::AlignVCenter);
}

void PageHeader::addActionWidget(QWidget* widget)
{
	m_actionLayout->addWidget(widget, 0, Qt::AlignVCenter);
}

void PageHeader::addActionSeparator()
{
	auto* divider = createDivider(Qt::Vertical);
	divider->setFixedHeight(20);
	m_actionLayout->addWidget(divider, 0, Qt::AlignVCenter);
}

void PageHeader::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
		refreshIcon();
	}
	QWidget::changeEvent(event);
}

void PageHeader::refreshIcon()
{
	if (!m_icon) {
		return;
	}
	const int side = scaledIconSize(QSize(22, 22)).width();
	m_icon->setFixedSize(side + 4, side + 4);
	m_icon->setPixmap(studioIconPixmap(m_iconName, side, devicePixelRatioF(), currentStudioTheme().colors.accent));
}

// ---------------------------------------------------------------------------
// EmptyStateView
// ---------------------------------------------------------------------------

EmptyStateView::EmptyStateView(const QString& iconName, const QString& title, const QString& body, QWidget* parent)
	: QWidget(parent)
	, m_iconName(iconName)
{
	setObjectName(QStringLiteral("emptyState"));
	setAccessibleName(title);
	setAccessibleDescription(body);

	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(24, 24, 24, 24);
	outer->addStretch(2);

	auto* column = new QWidget;
	column->setMaximumWidth(480);
	auto* layout = new QVBoxLayout(column);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	m_icon = new QLabel;
	m_icon->setAlignment(Qt::AlignCenter);
	m_icon->setFixedHeight(64);
	layout->addWidget(m_icon);

	m_title = new QLabel(title);
	m_title->setObjectName(QStringLiteral("emptyTitle"));
	m_title->setAlignment(Qt::AlignCenter);
	m_title->setWordWrap(true);
	layout->addWidget(m_title);

	m_body = new QLabel(body);
	m_body->setObjectName(QStringLiteral("emptyBody"));
	m_body->setAlignment(Qt::AlignCenter);
	m_body->setWordWrap(true);
	layout->addWidget(m_body);

	m_actions = new QHBoxLayout;
	m_actions->setContentsMargins(0, 8, 0, 0);
	m_actions->setSpacing(8);
	m_actions->addStretch(1);
	m_actions->addStretch(1);
	layout->addLayout(m_actions);

	auto* centerRow = new QHBoxLayout;
	centerRow->addStretch(1);
	centerRow->addWidget(column, 4);
	centerRow->addStretch(1);
	outer->addLayout(centerRow);
	outer->addStretch(3);

	refreshIcon();
}

void EmptyStateView::setTitle(const QString& title)
{
	m_title->setText(title);
	setAccessibleName(title);
}

void EmptyStateView::setBody(const QString& body)
{
	m_body->setText(body);
	setAccessibleDescription(body);
}

QPushButton* EmptyStateView::addAction(const QString& text, const QString& iconName, bool primary)
{
	QPushButton* button = createButton(text, iconName, primary ? QStringLiteral("primary") : QString());
	// Insert before the trailing stretch so the buttons stay centred.
	m_actions->insertWidget(m_actions->count() - 1, button);
	return button;
}

void EmptyStateView::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
		refreshIcon();
	}
	QWidget::changeEvent(event);
}

void EmptyStateView::refreshIcon()
{
	if (!m_icon) {
		return;
	}
	QColor color = currentStudioTheme().colors.textMuted;
	if (!currentStudioTheme().highContrast) {
		color.setAlphaF(0.7f);
	}
	const int side = scaledIconSize(QSize(56, 56)).width();
	m_icon->setFixedHeight(side + 8);
	m_icon->setPixmap(studioIconPixmap(m_iconName, side, devicePixelRatioF(), color));
}

// ---------------------------------------------------------------------------
// CardFrame
// ---------------------------------------------------------------------------

CardFrame::CardFrame(const QString& title, QWidget* parent)
	: QFrame(parent)
{
	setObjectName(QStringLiteral("card"));
	setAccessibleName(title);

	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(14, 12, 14, 14);
	root->setSpacing(10);

	m_header = new QHBoxLayout;
	m_header->setContentsMargins(0, 0, 0, 0);
	m_header->setSpacing(8);
	m_title = new QLabel(title);
	m_title->setObjectName(QStringLiteral("cardTitle"));
	m_header->addWidget(m_title, 0, Qt::AlignVCenter);
	m_meta = new ElidedLabel;
	m_meta->setObjectName(QStringLiteral("cardMeta"));
	m_meta->setVisible(false);
	m_header->addWidget(m_meta, 1, Qt::AlignVCenter);
	root->addLayout(m_header);

	m_body = new QVBoxLayout;
	m_body->setContentsMargins(0, 0, 0, 0);
	m_body->setSpacing(8);
	root->addLayout(m_body, 1);

	m_title->setVisible(!title.isEmpty());
}

void CardFrame::setTitle(const QString& title)
{
	m_title->setText(title);
	m_title->setVisible(!title.isEmpty());
	setAccessibleName(title);
}

void CardFrame::setMeta(const QString& meta)
{
	m_meta->setText(meta);
	m_meta->setVisible(!meta.isEmpty());
}

ElidedLabel* CardFrame::metaLabel() const
{
	return m_meta;
}

void CardFrame::addHeaderWidget(QWidget* widget)
{
	m_header->addWidget(widget, 0, Qt::AlignVCenter);
}

QVBoxLayout* CardFrame::bodyLayout() const
{
	return m_body;
}

// ---------------------------------------------------------------------------
// DockTitleBar
// ---------------------------------------------------------------------------

DockTitleBar::DockTitleBar(QDockWidget* dock)
	: QWidget(dock)
	, m_dock(dock)
{
	setObjectName(QStringLiteral("dockTitleBar"));
	setAttribute(Qt::WA_StyledBackground, true);
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(10, 2, 4, 2);
	layout->setSpacing(2);
	m_title = new ElidedLabel;
	m_title->setObjectName(QStringLiteral("dockTitle"));
	layout->addWidget(m_title, 1);
	m_float = createToolButton(QStringLiteral("external"), QString(), QString(), false);
	m_close = createToolButton(QStringLiteral("close"), QString(), QString(), false);
	for (QToolButton* button : {m_float, m_close}) {
		button->setObjectName(QStringLiteral("dockTitleButton"));
		setBaseIconSize(button, QSize(14, 14));
		layout->addWidget(button);
	}
	connect(m_float, &QToolButton::clicked, this, [this]() {
		if (m_dock) {
			m_dock->setFloating(!m_dock->isFloating());
		}
	});
	connect(m_close, &QToolButton::clicked, this, [this]() {
		if (m_dock) {
			m_dock->close();
		}
	});
	connect(dock, &QDockWidget::windowTitleChanged, this, [this]() {
		refresh();
	});
	connect(dock, &QDockWidget::topLevelChanged, this, [this]() {
		refresh();
	});
	connect(dock, &QDockWidget::featuresChanged, this, [this]() {
		refresh();
	});
	refresh();
}

void DockTitleBar::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::LanguageChange) {
		refresh();
	}
}

void DockTitleBar::refresh()
{
	if (!m_dock) {
		return;
	}
	const QString title = withoutMnemonic(m_dock->windowTitle());
	m_title->setText(title);
	const QDockWidget::DockWidgetFeatures features = m_dock->features();
	m_float->setVisible(features.testFlag(QDockWidget::DockWidgetFloatable));
	m_close->setVisible(features.testFlag(QDockWidget::DockWidgetClosable));
	const bool floating = m_dock->isFloating();
	m_float->setIcon(studioIcon(floating ? QStringLiteral("sidebar-right") : QStringLiteral("external")));
	const QString floatText = floating ? tr("Dock the %1 panel").arg(title) : tr("Float the %1 panel").arg(title);
	m_float->setToolTip(floatText);
	m_float->setAccessibleName(floatText);
	const QString closeText = tr("Close the %1 panel").arg(title);
	m_close->setToolTip(closeText);
	m_close->setAccessibleName(closeText);
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

QToolBar* createPageToolBar(const QString& accessibleName, QWidget* parent)
{
	auto* toolBar = new QToolBar(parent);
	toolBar->setObjectName(QStringLiteral("pageToolBar"));
	toolBar->setAccessibleName(accessibleName);
	toolBar->setMovable(false);
	toolBar->setFloatable(false);
	setBaseIconSize(toolBar, QSize(18, 18));
	toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	return toolBar;
}

QToolButton* createToolButton(const QString& iconName, const QString& text, const QString& toolTip, bool showText)
{
	auto* button = new QToolButton;
	if (!iconName.isEmpty()) {
		button->setIcon(studioIcon(iconName, StudioIconTone::Normal, showText ? StudioIconAlignment::Leading : StudioIconAlignment::Centre));
	}
	setBaseIconSize(button, showText ? QSize(23, 18) : QSize(18, 18));
	button->setText(text);
	button->setToolTip(toolTip.isEmpty() ? withoutMnemonic(text) : toolTip);
	button->setAccessibleName(withoutMnemonic(text));
	if (!toolTip.isEmpty()) {
		button->setAccessibleDescription(toolTip);
	}
	button->setToolButtonStyle(showText ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
	button->setAutoRaise(true);
	// Reachable with Tab; a mouse click leaves focus where the user was typing.
	button->setFocusPolicy(Qt::TabFocus);
	return button;
}

QPushButton* createButton(const QString& text, const QString& iconName, const QString& variant)
{
	auto* button = new QPushButton(text);
	const bool hasText = !text.trimmed().isEmpty();
	if (!iconName.isEmpty()) {
		button->setIcon(studioIcon(iconName, variant == QStringLiteral("primary") ? StudioIconTone::OnAccent : StudioIconTone::Normal,
			hasText ? StudioIconAlignment::Leading : StudioIconAlignment::Centre));
	}
	setBaseIconSize(button, hasText ? QSize(20, 16) : QSize(16, 16));
	button->setAccessibleName(withoutMnemonic(text));
	button->setFocusPolicy(Qt::TabFocus);
	if (!variant.isEmpty()) {
		setButtonVariant(button, variant);
	}
	return button;
}

QSplitter* createSplitter(Qt::Orientation orientation, const QString& objectName, const QString& accessibleName)
{
	auto* splitter = new QSplitter(orientation);
	splitter->setObjectName(objectName);
	splitter->setAccessibleName(accessibleName);
	splitter->setChildrenCollapsible(false);
	splitter->setHandleWidth(6);
	return splitter;
}

QTabWidget* createPanelTabs(const QString& accessibleName, QTabWidget::TabPosition position)
{
	auto* tabs = new QTabWidget;
	tabs->setObjectName(QStringLiteral("panelTabs"));
	tabs->setAccessibleName(accessibleName);
	tabs->setDocumentMode(true);
	tabs->setUsesScrollButtons(true);
	tabs->setElideMode(Qt::ElideRight);
	tabs->setTabPosition(position);
	tabs->setProperty("tabsAtBottom", position == QTabWidget::South);
	return tabs;
}

QScrollArea* createScrollSurface(QWidget* content, const QString& accessibleName)
{
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(accessibleName);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	content->setObjectName(QStringLiteral("scrollContent"));
	scroll->setWidget(content);
	return scroll;
}

QLabel* createStatusChip(const QString& accessibleName)
{
	auto* chip = new QLabel;
	chip->setObjectName(QStringLiteral("statusChip"));
	chip->setAccessibleName(accessibleName);
	chip->setAlignment(Qt::AlignCenter);
	chip->setProperty("operationState", QStringLiteral("idle"));
	// Long project or package names elide rather than widening the bar.
	chip->setMaximumWidth(260);
	return chip;
}

void setStatusChip(QLabel* chip, const QString& operationStateId, const QString& text, const QString& toolTip)
{
	if (!chip) {
		return;
	}
	const QString glyph = studioStateGlyph(operationStateFromId(operationStateId));
	const QString display = glyph.isEmpty() ? text : QStringLiteral("%1 %2").arg(glyph, text);
	const int maximum = chip->maximumWidth() < QWIDGETSIZE_MAX ? chip->maximumWidth() - 24 : 0;
	chip->setText(maximum > 0 ? chip->fontMetrics().elidedText(display, Qt::ElideMiddle, maximum) : display);
	chip->setToolTip(toolTip);
	chip->setAccessibleDescription(toolTip.isEmpty() ? text : toolTip);
	if (chip->property("operationState").toString() != operationStateId) {
		chip->setProperty("operationState", operationStateId);
		chip->style()->unpolish(chip);
		chip->style()->polish(chip);
	}
}

QFrame* createDivider(Qt::Orientation orientation)
{
	auto* divider = new QFrame;
	divider->setObjectName(QStringLiteral("railDivider"));
	divider->setFrameShape(QFrame::NoFrame);
	if (orientation == Qt::Vertical) {
		divider->setFixedWidth(1);
		divider->setStyleSheet(QString());
		divider->setObjectName(QStringLiteral("verticalDivider"));
	}
	divider->setAttribute(Qt::WA_StyledBackground, true);
	return divider;
}

void useElidingRows(QAbstractItemView* view, Qt::TextElideMode mode)
{
	if (!view) {
		return;
	}
	view->setItemDelegate(new ElidingItemDelegate(view));
	view->setTextElideMode(mode);
	view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

QSize scaledIconSize(const QSize& base)
{
	const double scale = std::clamp(currentStudioTheme().textScalePercent, 50, 400) / 100.0;
	return {static_cast<int>(base.width() * scale + 0.5), static_cast<int>(base.height() * scale + 0.5)};
}

void setBaseIconSize(QWidget* control, const QSize& size)
{
	if (!control) {
		return;
	}
	control->setProperty("studioBaseIconSize", size);
	const QSize scaled = scaledIconSize(size);
	if (auto* button = qobject_cast<QAbstractButton*>(control)) {
		button->setIconSize(scaled);
	} else if (auto* toolBar = qobject_cast<QToolBar*>(control)) {
		toolBar->setIconSize(scaled);
	} else if (auto* view = qobject_cast<QAbstractItemView*>(control)) {
		view->setIconSize(scaled);
	}
}

void applyStudioIconScale(QWidget* root)
{
	if (!root) {
		return;
	}
	for (QWidget* widget : root->findChildren<QWidget*>()) {
		const QVariant base = widget->property("studioBaseIconSize");
		if (base.isValid()) {
			setBaseIconSize(widget, base.toSize());
			continue;
		}
		// Item views without a recorded size use the small-icon default.
		if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
			view->setIconSize(scaledIconSize(QSize(16, 16)));
		}
	}
}

QWidget* createLabeledField(const QString& caption, QWidget* field)
{
	auto* container = new QWidget;
	auto* layout = new QVBoxLayout(container);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(3);
	auto* label = new QLabel(caption);
	label->setObjectName(QStringLiteral("fieldHint"));
	label->setBuddy(field);
	layout->addWidget(label);
	layout->addWidget(field);
	return container;
}

} // namespace vibestudio
