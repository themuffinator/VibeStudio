#include "app/studio_sidebar.h"

#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QContextMenuEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

const QString kTabIcon = QStringLiteral("icon");
const QString kTabGroup = QStringLiteral("group");

// The tile around each glyph leaves this much room on every side of it.
int tilePaddingFor(int glyph)
{
	return std::max(6, (glyph * 2 + 2) / 5);
}

int columnMarginFor(int glyph)
{
	return std::max(3, glyph / 5);
}

QFont captionFont(const QFont& base)
{
	QFont font = base;
	if (font.pointSizeF() > 0) {
		font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.84));
	} else if (font.pixelSize() > 0) {
		font.setPixelSize(std::max(9, static_cast<int>(font.pixelSize() * 0.84)));
	}
	return font;
}

// Lays a caption out in at most two lines of `width`, the second elided.
QStringList captionLines(const QString& text, const QFontMetrics& metrics, int width)
{
	if (width <= 0 || text.isEmpty()) {
		return {};
	}
	if (metrics.horizontalAdvance(text) <= width) {
		return {text};
	}
	// Break at the last space that fits; a single long word is elided.
	const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	QString first;
	int used = 0;
	for (; used < words.size(); ++used) {
		const QString candidate = first.isEmpty() ? words.at(used) : first + QLatin1Char(' ') + words.at(used);
		if (metrics.horizontalAdvance(candidate) > width) {
			break;
		}
		first = candidate;
	}
	if (first.isEmpty()) {
		return {metrics.elidedText(text, Qt::ElideRight, width)};
	}
	const QString rest = QStringList(words.mid(used)).join(QLatin1Char(' '));
	if (rest.isEmpty()) {
		return {first};
	}
	return {first, metrics.elidedText(rest, Qt::ElideRight, width)};
}

class SidebarSectionHeaderAccessible final : public QAccessibleWidget {
public:
	explicit SidebarSectionHeaderAccessible(SidebarSectionHeader* header)
		: QAccessibleWidget(header, QAccessible::Button)
	{
	}

	QAccessible::State state() const override
	{
		QAccessible::State state = QAccessibleWidget::state();
		const auto* header = static_cast<const SidebarSectionHeader*>(widget());
		state.expandable = true;
		state.expanded = header->isChecked();
		state.collapsed = !header->isChecked();
		state.focusable = header->focusPolicy() != Qt::NoFocus;
		return state;
	}

	QString text(QAccessible::Text type) const override
	{
		const auto* header = static_cast<const SidebarSectionHeader*>(widget());
		if (type == QAccessible::Name) {
			const QString name = header->accessibleName();
			return name.isEmpty() ? header->text() : name;
		}
		return QAccessibleWidget::text(type);
	}

	QStringList actionNames() const override
	{
		return {QAccessibleActionInterface::pressAction(), QAccessibleActionInterface::toggleAction()};
	}

	void doAction(const QString& actionName) override
	{
		if (actionName == QAccessibleActionInterface::pressAction() || actionName == QAccessibleActionInterface::toggleAction()) {
			static_cast<SidebarSectionHeader*>(widget())->click();
		}
	}
};

QAccessibleInterface* sidebarAccessibility(const QString&, QObject* object)
{
	if (auto* header = qobject_cast<SidebarSectionHeader*>(object)) {
		return new SidebarSectionHeaderAccessible(header);
	}
	return nullptr;
}

void installSidebarAccessibility()
{
	static bool installed = false;
	if (!installed) {
		installed = true;
		QAccessible::installFactory(sidebarAccessibility);
	}
}

QToolButton* createHeaderButton(const QString& iconName, const QString& text, const QString& toolTip)
{
	auto* button = new QToolButton;
	button->setObjectName(QStringLiteral("sidebarHeaderButton"));
	button->setIcon(studioIcon(iconName));
	setBaseIconSize(button, QSize(16, 16));
	button->setToolButtonStyle(Qt::ToolButtonIconOnly);
	button->setAutoRaise(true);
	button->setText(text);
	button->setAccessibleName(text);
	button->setToolTip(toolTip.isEmpty() ? text : toolTip);
	if (!toolTip.isEmpty()) {
		button->setAccessibleDescription(toolTip);
	}
	button->setFocusPolicy(Qt::TabFocus);
	return button;
}

} // namespace

// ---------------------------------------------------------------------------
// SidebarTabBar
// ---------------------------------------------------------------------------

SidebarTabBar::SidebarTabBar(QWidget* parent)
	: QTabBar(parent)
{
	setObjectName(QStringLiteral("sidebarTabBar"));
	setDrawBase(false);
	setExpanding(false);
	setUsesScrollButtons(true);
	setElideMode(Qt::ElideRight);
	setFocusPolicy(Qt::TabFocus);
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void SidebarTabBar::setGroupStart(int index, bool starts)
{
	if (index < 0 || index >= count()) {
		return;
	}
	QVariantMap data = tabData(index).toMap();
	if (data.value(kTabGroup).toBool() == starts) {
		return;
	}
	data.insert(kTabGroup, starts);
	setTabData(index, data);
	updateGeometry();
	update();
}

bool SidebarTabBar::startsGroup(int index) const
{
	return index > 0 && index < count() && tabData(index).toMap().value(kTabGroup).toBool();
}

void SidebarTabBar::setShowLabels(bool show)
{
	if (m_showLabels == show) {
		return;
	}
	m_showLabels = show;
	// QTabBar caches each tab's size; a changed font is what makes it ask again.
	QEvent change(QEvent::StyleChange);
	QTabBar::changeEvent(&change);
	updateGeometry();
	update();
}

bool SidebarTabBar::showsLabels() const
{
	return m_showLabels;
}

void SidebarTabBar::setFolded(bool folded)
{
	if (m_folded != folded) {
		m_folded = folded;
		update();
	}
}

bool SidebarTabBar::isFolded() const
{
	return m_folded;
}

int SidebarTabBar::glyphSize() const
{
	return scaledIconSize(QSize(20, 20)).width();
}

int SidebarTabBar::groupGap() const
{
	return columnMarginFor(glyphSize()) * 3;
}

int SidebarTabBar::columnWidth() const
{
	const int glyph = glyphSize();
	const int tile = glyph + 2 * tilePaddingFor(glyph);
	const int width = tile + 2 * columnMarginFor(glyph);
	if (!m_showLabels) {
		return width;
	}
	// Room for a caption of about ten average characters under the glyph.
	const QFontMetrics metrics(captionFont(font()));
	return std::max(width, metrics.averageCharWidth() * 10 + 2 * columnMarginFor(glyph));
}

QSize SidebarTabBar::tabSizeHint(int index) const
{
	const int glyph = glyphSize();
	int height = glyph + 2 * tilePaddingFor(glyph) + 2;
	if (m_showLabels) {
		height += QFontMetrics(captionFont(font())).height() * 2 + 2;
	}
	if (startsGroup(index)) {
		height += groupGap();
	}
	return {columnWidth(), height};
}

QSize SidebarTabBar::minimumTabSizeHint(int index) const
{
	return tabSizeHint(index);
}

bool SidebarTabBar::pageOnRight() const
{
	// The page lies across the column from the tabs' outer edge.
	return shape() == QTabBar::RoundedWest || shape() == QTabBar::TriangularWest;
}

QRect SidebarTabBar::tileRect(int index) const
{
	QRect tab = tabRect(index);
	if (tab.isNull()) {
		return {};
	}
	if (startsGroup(index)) {
		tab.setTop(tab.top() + groupGap());
	}
	const int margin = columnMarginFor(glyphSize());
	return tab.adjusted(margin, 1, -margin, -1);
}

int SidebarTabBar::tabAtPosition(const QPoint& position) const
{
	for (int index = 0; index < count(); ++index) {
		QRect tab = tabRect(index);
		if (startsGroup(index)) {
			tab.setTop(tab.top() + groupGap());
		}
		if (tab.contains(position)) {
			return index;
		}
	}
	return -1;
}

void SidebarTabBar::paintEvent(QPaintEvent*)
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	const StudioThemeColors& colors = tokens.colors;
	QPainter painter(this);
	painter.fillRect(rect(), colors.appBackground);
	const bool right = pageOnRight();
	const int edgeX = right ? width() - 1 : 0;
	const int glyph = glyphSize();
	const qreal radius = std::max(3, tokens.metrics.radiusSmall);
	const int current = currentIndex();

	// The rule between the column and its page, broken where the current tab
	// opens into the page.
	QRect joined;
	if (!m_folded && current >= 0) {
		joined = tileRect(current);
	}
	painter.setPen(Qt::NoPen);
	if (joined.isValid()) {
		painter.fillRect(QRect(edgeX, 0, 1, joined.top()), colors.borderSubtle);
		painter.fillRect(QRect(edgeX, joined.bottom() + 1, 1, height() - joined.bottom() - 1), colors.borderSubtle);
	} else {
		painter.fillRect(QRect(edgeX, 0, 1, height()), colors.borderSubtle);
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	const QFont caption = captionFont(font());
	const QFontMetrics captionMetrics(caption);
	for (int index = 0; index < count(); ++index) {
		const QRect tile = tileRect(index);
		if (!tile.isValid() || !tile.intersects(rect())) {
			continue;
		}
		if (startsGroup(index)) {
			const int y = tabRect(index).top() + groupGap() / 2;
			const int inset = width() / 4;
			painter.fillRect(QRect(inset, y, width() - inset * 2, 1), colors.borderSubtle);
		}
		const bool isCurrent = index == current;
		const bool hovered = index == m_hover && !isCurrent;
		const bool enabled = isTabEnabled(index);
		QColor glyphColor = enabled ? colors.textMuted : colors.textFaint;
		if (isCurrent && !m_folded) {
			// Joined to the page: the tile takes the page's colour and runs on
			// to the column's edge.
			QRectF shape = QRectF(tile).adjusted(0.5, 0.5, -0.5, -0.5);
			if (right) {
				shape.setRight(width());
			} else {
				shape.setLeft(0);
			}
			QPainterPath path;
			// Winding, so the squared corners add to the rounded tile rather
			// than cancel out where they overlap.
			path.setFillRule(Qt::WindingFill);
			path.addRoundedRect(shape, radius, radius);
			// Square off the corners that meet the page.
			const QRectF square = right ? QRectF(shape.right() - radius * 2, shape.top(), radius * 2, shape.height())
										: QRectF(shape.left(), shape.top(), radius * 2, shape.height());
			path.addRect(square);
			painter.fillPath(path.simplified(), colors.surface);
			if (tokens.highContrast) {
				painter.setPen(QPen(colors.accent, std::max(2, tokens.metrics.focusWidth)));
				painter.setBrush(Qt::NoBrush);
				painter.drawPath(path.simplified());
				painter.setPen(Qt::NoPen);
			}
			glyphColor = tokens.highContrast ? colors.text : colors.accent;
		} else if (isCurrent) {
			painter.setBrush(colors.accentSubtle);
			painter.drawRoundedRect(QRectF(tile), radius, radius);
			glyphColor = tokens.highContrast ? colors.selectionText : colors.accent;
		} else if (hovered && enabled) {
			painter.setBrush(colors.panelRaised);
			painter.drawRoundedRect(QRectF(tile), radius, radius);
			glyphColor = colors.text;
		}
		if (isCurrent) {
			// The accent mark on the outer edge, as the mode rail marks its page.
			const int barHeight = std::max(10, std::min(tile.height(), glyph + tilePaddingFor(glyph)) - 8);
			const qreal barX = right ? 1.0 : width() - 4.0;
			const qreal top = tile.top() + (std::min(tile.height(), glyph + 2 * tilePaddingFor(glyph)) - barHeight) / 2.0;
			painter.setBrush(colors.accent);
			painter.drawRoundedRect(QRectF(barX, top, 3.0, barHeight), 1.5, 1.5);
		}
		const QString iconName = tabData(index).toMap().value(kTabIcon).toString();
		const int tileGlyphTop = tile.top() + tilePaddingFor(glyph);
		const QRect glyphRect(tile.left() + (tile.width() - glyph) / 2, tileGlyphTop, glyph, glyph);
		if (!iconName.isEmpty()) {
			painter.drawPixmap(glyphRect, studioIconPixmap(iconName, glyph, devicePixelRatioF(), glyphColor));
		}
		if (m_showLabels) {
			painter.setFont(caption);
			painter.setPen(isCurrent ? colors.text : glyphColor);
			const int textWidth = tile.width() - 4;
			const QStringList lines = captionLines(tabText(index), captionMetrics, textWidth);
			int y = glyphRect.bottom() + 1 + std::max(2, tilePaddingFor(glyph) / 2);
			for (const QString& line : lines) {
				painter.drawText(QRect(tile.left() + 2, y, textWidth, captionMetrics.height()), Qt::AlignHCenter | Qt::AlignTop, line);
				y += captionMetrics.height();
			}
			painter.setPen(Qt::NoPen);
		}
		if (isCurrent && hasFocus()) {
			QPen focus(colors.focus, std::max(1, tokens.metrics.focusWidth));
			painter.setPen(focus);
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(QRectF(tile).adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
			painter.setPen(Qt::NoPen);
		}
	}
}

void SidebarTabBar::mousePressEvent(QMouseEvent* event)
{
	// A press in the gap between groups is not a press on the tab below it.
	if (event->button() == Qt::LeftButton && tabAtPosition(event->position().toPoint()) < 0) {
		event->accept();
		return;
	}
	QTabBar::mousePressEvent(event);
}

void SidebarTabBar::mouseMoveEvent(QMouseEvent* event)
{
	const int hover = tabAtPosition(event->position().toPoint());
	if (hover != m_hover) {
		m_hover = hover;
		update();
	}
	QTabBar::mouseMoveEvent(event);
}

void SidebarTabBar::leaveEvent(QEvent* event)
{
	if (m_hover != -1) {
		m_hover = -1;
		update();
	}
	QTabBar::leaveEvent(event);
}

void SidebarTabBar::contextMenuEvent(QContextMenuEvent* event)
{
	int index = -1;
	QPoint global = event->globalPos();
	if (event->reason() == QContextMenuEvent::Keyboard) {
		index = currentIndex();
		const QRect tile = tileRect(index);
		global = mapToGlobal(tile.isValid() ? tile.center() : rect().center());
	} else {
		index = tabAtPosition(event->pos());
	}
	event->accept();
	emit tabMenuRequested(index, global);
}

void SidebarTabBar::changeEvent(QEvent* event)
{
	QTabBar::changeEvent(event);
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		updateGeometry();
		update();
	}
}

// ---------------------------------------------------------------------------
// StudioSidebar
// ---------------------------------------------------------------------------

StudioSidebar::StudioSidebar(const QString& accessibleName, TabEdge edge, QWidget* parent)
	: QTabWidget(parent)
	, m_edge(edge)
{
	setObjectName(QStringLiteral("studioSidebar"));
	// Styled by property, since owners give each sidebar a name of its own.
	setProperty("studioSidebar", true);
	setAccessibleName(accessibleName);
	m_bar = new SidebarTabBar(this);
	m_bar->setAccessibleName(accessibleName);
	setTabBar(m_bar);
	setDocumentMode(true);
	setUsesScrollButtons(true);
	setElideMode(Qt::ElideRight);
	applyTabPosition();
	connect(m_bar, &QTabBar::tabBarClicked, this, [this](int index) {
		if (index < 0) {
			return;
		}
		// The current tab folds the sidebar away and brings it back; another
		// tab always opens it on that page.
		if (index == currentIndex()) {
			setFolded(!m_folded);
		} else if (m_folded) {
			setFolded(false);
		}
	});
	connect(this, &QTabWidget::currentChanged, this, [this](int) {
		// A page chosen from elsewhere, a command or a search, is shown.
		if (m_folded && !m_applyingFold) {
			setFolded(false);
		}
		emit currentPageChanged(currentPageId());
	});
	connect(m_bar, &SidebarTabBar::tabMenuRequested, this, [this](int index, const QPoint& global) {
		SidebarPage* target = pageAt(index);
		emit tabMenuRequested(target ? target->pageId() : QString(), global);
	});
}

SidebarTabBar* StudioSidebar::sidebarTabBar() const
{
	return m_bar;
}

StudioSidebar::TabEdge StudioSidebar::tabEdge() const
{
	return m_edge;
}

void StudioSidebar::setTabEdge(TabEdge edge)
{
	if (m_edge != edge) {
		m_edge = edge;
		applyTabPosition();
	}
}

void StudioSidebar::applyTabPosition()
{
	// QTabWidget keeps West on the left in either direction, so the logical
	// edge is resolved here.
	const bool leftToRight = layoutDirection() != Qt::RightToLeft;
	const bool onLeft = (m_edge == TabEdge::Leading) == leftToRight;
	const TabPosition position = onLeft ? QTabWidget::West : QTabWidget::East;
	if (tabPosition() != position) {
		setTabPosition(position);
	}
	m_bar->update();
}

int StudioSidebar::addPage(SidebarPage* page, bool startsGroup)
{
	if (!page) {
		return -1;
	}
	const int index = addTab(page, page->title());
	m_bar->setGroupStart(index, startsGroup);
	connect(page, &SidebarPage::presentationChanged, this, [this, page]() {
		refreshTab(indexOf(page));
	});
	refreshTab(index);
	return index;
}

SidebarPage* StudioSidebar::takePage(const QString& pageId)
{
	const int index = indexOfPage(pageId);
	if (index < 0) {
		return nullptr;
	}
	SidebarPage* taken = pageAt(index);
	disconnect(taken, &SidebarPage::presentationChanged, this, nullptr);
	removeTab(index);
	return taken;
}

SidebarPage* StudioSidebar::page(const QString& pageId) const
{
	return pageAt(indexOfPage(pageId));
}

SidebarPage* StudioSidebar::pageAt(int index) const
{
	return index >= 0 && index < count() ? qobject_cast<SidebarPage*>(widget(index)) : nullptr;
}

QStringList StudioSidebar::pageIds() const
{
	QStringList ids;
	for (int index = 0; index < count(); ++index) {
		if (const SidebarPage* entry = pageAt(index)) {
			ids << entry->pageId();
		}
	}
	return ids;
}

int StudioSidebar::indexOfPage(const QString& pageId) const
{
	for (int index = 0; index < count(); ++index) {
		if (const SidebarPage* entry = pageAt(index); entry && entry->pageId() == pageId) {
			return index;
		}
	}
	return -1;
}

QString StudioSidebar::currentPageId() const
{
	const SidebarPage* entry = pageAt(currentIndex());
	return entry ? entry->pageId() : QString();
}

bool StudioSidebar::showPage(const QString& pageId)
{
	const int index = indexOfPage(pageId);
	if (index < 0) {
		return false;
	}
	setCurrentIndex(index);
	setFolded(false);
	return true;
}

void StudioSidebar::refreshTab(int index)
{
	SidebarPage* entry = pageAt(index);
	if (!entry) {
		return;
	}
	QVariantMap data = m_bar->tabData(index).toMap();
	data.insert(kTabIcon, entry->iconName());
	m_bar->setTabData(index, data);
	if (m_bar->tabText(index) != entry->title()) {
		m_bar->setTabText(index, entry->title());
	}
	m_bar->setAccessibleTabName(index, entry->title());
	QString tip = entry->title();
	if (!entry->badge().isEmpty()) {
		tip = tr("%1 (%2)").arg(tip, entry->badge());
	}
	if (!entry->shortcutText().isEmpty()) {
		tip = tr("%1  %2").arg(tip, entry->shortcutText());
	}
	if (!entry->description().isEmpty()) {
		tip += QLatin1Char('\n') + entry->description();
	}
	m_bar->setTabToolTip(index, tip);
	m_bar->setTabWhatsThis(index, entry->description());
	m_bar->update();
}

QStackedWidget* StudioSidebar::stack() const
{
	return findChild<QStackedWidget*>(QStringLiteral("qt_tabwidget_stackedwidget"), Qt::FindDirectChildrenOnly);
}

int StudioSidebar::foldedWidth() const
{
	return m_bar->columnWidth();
}

int StudioSidebar::expandedWidth() const
{
	return m_expandedWidth;
}

void StudioSidebar::setExpandedWidth(int width)
{
	m_expandedWidth = std::max(0, width);
}

bool StudioSidebar::isFolded() const
{
	return m_folded;
}

void StudioSidebar::setShowLabels(bool show)
{
	m_bar->setShowLabels(show);
	refreshFoldedGeometry();
	updateGeometry();
}

bool StudioSidebar::showsLabels() const
{
	return m_bar->showsLabels();
}

void StudioSidebar::refreshFoldedGeometry()
{
	if (m_folded) {
		const int width = foldedWidth();
		setMinimumWidth(width);
		setMaximumWidth(width);
	} else {
		setMaximumWidth(QWIDGETSIZE_MAX);
		setMinimumWidth(0);
	}
}

void StudioSidebar::setFolded(bool folded)
{
	if (m_folded == folded) {
		return;
	}
	const QScopedValueRollback<bool> applying(m_applyingFold, true);
	auto* splitter = qobject_cast<QSplitter*>(parentWidget());
	QList<int> sizes = splitter ? splitter->sizes() : QList<int>();
	const int self = splitter ? splitter->indexOf(this) : -1;
	if (folded && width() > foldedWidth() + 8) {
		m_expandedWidth = width();
	}
	m_folded = folded;
	m_bar->setFolded(folded);
	if (QStackedWidget* pages = stack()) {
		pages->setVisible(!folded);
	}
	refreshFoldedGeometry();
	if (splitter && self >= 0 && sizes.size() == splitter->count()) {
		const int target = folded ? foldedWidth() : std::max(m_expandedWidth, QTabWidget::minimumSizeHint().width());
		const int delta = target - sizes.at(self);
		// The widest neighbour, the work area, takes or gives the difference.
		int widest = -1;
		for (int index = 0; index < sizes.size(); ++index) {
			if (index != self && (widest < 0 || sizes.at(index) > sizes.at(widest))) {
				widest = index;
			}
		}
		if (widest >= 0) {
			sizes[widest] = std::max(0, sizes.at(widest) - delta);
			sizes[self] = target;
			splitter->setSizes(sizes);
		}
	}
	updateGeometry();
	emit foldedChanged(folded);
}

QSize StudioSidebar::minimumSizeHint() const
{
	if (m_folded) {
		return {foldedWidth(), m_bar->minimumSizeHint().height()};
	}
	return QTabWidget::minimumSizeHint();
}

QSize StudioSidebar::sizeHint() const
{
	if (m_folded) {
		return {foldedWidth(), QTabWidget::sizeHint().height()};
	}
	return QTabWidget::sizeHint();
}

void StudioSidebar::changeEvent(QEvent* event)
{
	QTabWidget::changeEvent(event);
	switch (event->type()) {
	case QEvent::LayoutDirectionChange:
		applyTabPosition();
		break;
	case QEvent::FontChange:
	case QEvent::StyleChange:
		refreshFoldedGeometry();
		updateGeometry();
		break;
	default:
		break;
	}
}

void StudioSidebar::resizeEvent(QResizeEvent* event)
{
	QTabWidget::resizeEvent(event);
	if (!m_folded && width() > foldedWidth() + 8) {
		m_expandedWidth = width();
	}
}

void StudioSidebar::tabInserted(int index)
{
	QTabWidget::tabInserted(index);
	m_bar->updateGeometry();
}

void StudioSidebar::tabRemoved(int index)
{
	QTabWidget::tabRemoved(index);
	m_bar->updateGeometry();
}

// ---------------------------------------------------------------------------
// SidebarSectionHeader
// ---------------------------------------------------------------------------

SidebarSectionHeader::SidebarSectionHeader(const QString& title, QWidget* parent)
	: QAbstractButton(parent)
{
	installSidebarAccessibility();
	setObjectName(QStringLiteral("sidebarSectionHeader"));
	setText(title);
	setCheckable(true);
	setChecked(true);
	// Reachable with Tab; a click leaves focus where the user was working.
	setFocusPolicy(Qt::TabFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	connect(this, &QAbstractButton::toggled, this, [this](bool open) {
		QAccessible::State changed;
		changed.expanded = true;
		changed.collapsed = true;
		QAccessibleStateChangeEvent event(this, changed);
		QAccessible::updateAccessibility(&event);
		setAccessibleDescription(open ? tr("Section, expanded") : tr("Section, collapsed"));
		update();
	});
	setAccessibleDescription(tr("Section, expanded"));
}

void SidebarSectionHeader::setBadge(const QString& badge)
{
	if (m_badge != badge) {
		m_badge = badge;
		updateGeometry();
		update();
	}
}

QString SidebarSectionHeader::badge() const
{
	return m_badge;
}

QSize SidebarSectionHeader::sizeHint() const
{
	const QFontMetrics metrics(font());
	const int chevron = scaledIconSize(QSize(14, 14)).width();
	const int height = std::max(metrics.height() + 12, scaledIconSize(QSize(26, 26)).height());
	int width = 8 + chevron + 6 + metrics.horizontalAdvance(text()) + 8;
	if (!m_badge.isEmpty()) {
		width += 8 + metrics.horizontalAdvance(m_badge);
	}
	return {width, height};
}

QSize SidebarSectionHeader::minimumSizeHint() const
{
	const QFontMetrics metrics(font());
	const int chevron = scaledIconSize(QSize(14, 14)).width();
	return {8 + chevron + 6 + metrics.horizontalAdvance(QStringLiteral("…")) * 3, sizeHint().height()};
}

void SidebarSectionHeader::paintEvent(QPaintEvent*)
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setLayoutDirection(layoutDirection());
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	const int chevron = scaledIconSize(QSize(14, 14)).width();
	const QColor color = !isEnabled() ? tokens.colors.textFaint : (m_hovered || hasFocus() ? tokens.colors.text : tokens.colors.text);
	const QColor muted = isEnabled() ? tokens.colors.textMuted : tokens.colors.textFaint;
	// Left to right, then mirrored.
	const auto visual = [this, mirrored](const QRect& logical) {
		return mirrored ? QRect(width() - logical.right() - 1, logical.top(), logical.width(), logical.height()) : logical;
	};
	const QRect chevronRect = visual(QRect(8, (height() - chevron) / 2, chevron, chevron));
	const QString glyph = isChecked() ? QStringLiteral("chevron-down") : (mirrored ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"));
	painter.drawPixmap(chevronRect, studioIconPixmap(glyph, chevron, devicePixelRatioF(), m_hovered ? tokens.colors.text : muted));
	QFont titleFont = font();
	titleFont.setWeight(QFont::DemiBold);
	painter.setFont(titleFont);
	const QFontMetrics titleMetrics(titleFont);
	const int textLeft = 8 + chevron + 6;
	int available = width() - textLeft - 8;
	QString badge = m_badge;
	int badgeWidth = 0;
	if (!badge.isEmpty()) {
		badgeWidth = QFontMetrics(font()).horizontalAdvance(badge);
		if (badgeWidth > available / 2) {
			badge.clear();
			badgeWidth = 0;
		} else {
			available -= badgeWidth + 8;
		}
	}
	const QString title = titleMetrics.elidedText(text(), Qt::ElideRight, std::max(0, available));
	const int titleWidth = std::min(available, titleMetrics.horizontalAdvance(title));
	painter.setPen(color);
	painter.drawText(visual(QRect(textLeft, 0, titleWidth + 1, height())), Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine, title);
	if (!badge.isEmpty()) {
		painter.setFont(font());
		painter.setPen(muted);
		painter.drawText(visual(QRect(textLeft + titleWidth + 8, 0, badgeWidth + 1, height())), Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine, badge);
	}
	if (hasFocus()) {
		QPen focus(tokens.colors.focus, std::max(1, tokens.metrics.focusWidth));
		painter.setPen(focus);
		painter.setBrush(Qt::NoBrush);
		const qreal radius = std::max(3, tokens.metrics.radiusSmall);
		painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
	}
}

void SidebarSectionHeader::keyPressEvent(QKeyEvent* event)
{
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	switch (event->key()) {
	case Qt::Key_Return:
	case Qt::Key_Enter:
		click();
		event->accept();
		return;
	case Qt::Key_Left:
	case Qt::Key_Right: {
		// Towards the reading direction opens; against it folds.
		const bool open = (event->key() == Qt::Key_Right) != mirrored;
		if (isChecked() != open) {
			click();
		}
		event->accept();
		return;
	}
	default:
		break;
	}
	QAbstractButton::keyPressEvent(event);
}

void SidebarSectionHeader::enterEvent(QEnterEvent* event)
{
	m_hovered = true;
	update();
	QAbstractButton::enterEvent(event);
}

void SidebarSectionHeader::leaveEvent(QEvent* event)
{
	m_hovered = false;
	update();
	QAbstractButton::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// SidebarSection
// ---------------------------------------------------------------------------

SidebarSection::SidebarSection(const QString& sectionId, const QString& title, QWidget* body, QWidget* parent)
	: QWidget(parent)
	, m_id(sectionId)
	, m_body(body)
{
	setObjectName(QStringLiteral("sidebarSection"));
	setProperty("sectionId", sectionId);
	setAccessibleName(title);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(1, 1, 1, 1);
	layout->setSpacing(0);
	auto* headerRow = new QWidget;
	headerRow->setObjectName(QStringLiteral("sidebarSectionHeaderRow"));
	m_headerLayout = new QHBoxLayout(headerRow);
	m_headerLayout->setContentsMargins(0, 0, 4, 0);
	m_headerLayout->setSpacing(2);
	m_header = new SidebarSectionHeader(title);
	m_header->setAccessibleName(title);
	m_headerLayout->addWidget(m_header, 1);
	layout->addWidget(headerRow);
	m_bodyFrame = new QWidget;
	m_bodyFrame->setObjectName(QStringLiteral("sidebarSectionBody"));
	auto* bodyLayout = new QVBoxLayout(m_bodyFrame);
	bodyLayout->setContentsMargins(8, 0, 8, 8);
	bodyLayout->setSpacing(6);
	if (body) {
		bodyLayout->addWidget(body, 1);
	}
	layout->addWidget(m_bodyFrame, 1);
	connect(m_header, &QAbstractButton::toggled, this, [this](bool open) {
		m_bodyFrame->setVisible(open);
		updateGeometry();
		emit expandedChanged(open);
	});
}

QString SidebarSection::sectionId() const
{
	return m_id;
}

QString SidebarSection::title() const
{
	return m_header->text();
}

void SidebarSection::setTitle(const QString& title)
{
	m_header->setText(title);
	m_header->setAccessibleName(title);
	setAccessibleName(title);
	m_header->updateGeometry();
}

void SidebarSection::setBadge(const QString& badge)
{
	m_header->setBadge(badge);
}

QWidget* SidebarSection::body() const
{
	return m_body;
}

SidebarSectionHeader* SidebarSection::header() const
{
	return m_header;
}

QToolButton* SidebarSection::addHeaderButton(const QString& iconName, const QString& text, const QString& toolTip)
{
	QToolButton* button = createHeaderButton(iconName, text, toolTip);
	m_headerLayout->addWidget(button);
	return button;
}

void SidebarSection::setExpanded(bool expanded)
{
	if (m_header->isChecked() != expanded) {
		m_header->setChecked(expanded);
	}
}

bool SidebarSection::isExpanded() const
{
	return m_header->isChecked();
}

void SidebarSection::setStretch(int stretch)
{
	m_stretch = std::max(0, stretch);
	// A section that does not stretch keeps its natural height, so a short
	// page scrolls instead of clipping its buttons.
	setSizePolicy(QSizePolicy::Preferred, m_stretch > 0 ? QSizePolicy::Expanding : QSizePolicy::Minimum);
}

int SidebarSection::stretch() const
{
	return m_stretch;
}

void SidebarSection::paintEvent(QPaintEvent*)
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const qreal radius = std::max(3, tokens.metrics.radiusSmall);
	const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(QPen(tokens.highContrast ? tokens.colors.border : tokens.colors.borderSubtle, 1.0));
	painter.setBrush(tokens.colors.panel);
	painter.drawRoundedRect(box, radius, radius);
}

// ---------------------------------------------------------------------------
// SidebarPage
// ---------------------------------------------------------------------------

SidebarPage::SidebarPage(const QString& pageId, const QString& iconName, const QString& title, const QString& description, QWidget* parent)
	: QWidget(parent)
	, m_id(pageId)
	, m_iconName(iconName)
	, m_title(title)
	, m_description(description)
{
	setObjectName(QStringLiteral("sidebarPage"));
	setProperty("sidebarPage", true);
	setAttribute(Qt::WA_StyledBackground, true);
	setProperty("pageId", pageId);
	setAccessibleName(title);
	setAccessibleDescription(description);
	m_rootLayout = new QVBoxLayout(this);
	m_rootLayout->setContentsMargins(0, 0, 0, 0);
	m_rootLayout->setSpacing(0);
	auto* header = new QWidget;
	header->setObjectName(QStringLiteral("sidebarPageHeader"));
	m_headerLayout = new QHBoxLayout(header);
	m_headerLayout->setContentsMargins(12, 6, 6, 6);
	m_headerLayout->setSpacing(4);
	auto* titleLabel = new ElidedLabel(title);
	titleLabel->setObjectName(QStringLiteral("sidebarPageTitle"));
	titleLabel->setTextFormat(Qt::PlainText);
	m_titleLabel = titleLabel;
	m_badgeLabel = new QLabel;
	m_badgeLabel->setObjectName(QStringLiteral("sidebarPageBadge"));
	m_badgeLabel->setTextFormat(Qt::PlainText);
	m_badgeLabel->hide();
	m_headerLayout->addWidget(m_titleLabel, 1);
	m_headerLayout->addWidget(m_badgeLabel);
	m_rootLayout->addWidget(header);
	m_content = new QWidget;
	m_content->setObjectName(QStringLiteral("sidebarPageContent"));
	m_contentLayout = new QVBoxLayout(m_content);
	m_contentLayout->setContentsMargins(8, 8, 8, 8);
	m_contentLayout->setSpacing(6);
	m_contentLayout->addStretch(1);
	m_scroll = new QScrollArea;
	m_scroll->setObjectName(QStringLiteral("sidebarPageScroll"));
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setWidgetResizable(true);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->setWidget(m_content);
	m_rootLayout->addWidget(m_scroll, 1);
}

QString SidebarPage::pageId() const
{
	return m_id;
}

QString SidebarPage::iconName() const
{
	return m_iconName;
}

QString SidebarPage::title() const
{
	return m_title;
}

void SidebarPage::setTitle(const QString& title)
{
	if (m_title == title) {
		return;
	}
	m_title = title;
	setAccessibleName(title);
	refreshHeader();
	emit presentationChanged();
}

QString SidebarPage::description() const
{
	return m_description;
}

void SidebarPage::setDescription(const QString& description)
{
	if (m_description == description) {
		return;
	}
	m_description = description;
	setAccessibleDescription(description);
	emit presentationChanged();
}

void SidebarPage::setBadge(const QString& badge)
{
	if (m_badge == badge) {
		return;
	}
	m_badge = badge;
	refreshHeader();
	emit presentationChanged();
}

QString SidebarPage::badge() const
{
	return m_badge;
}

void SidebarPage::setShortcutText(const QString& shortcut)
{
	if (m_shortcut != shortcut) {
		m_shortcut = shortcut;
		emit presentationChanged();
	}
}

QString SidebarPage::shortcutText() const
{
	return m_shortcut;
}

void SidebarPage::refreshHeader()
{
	static_cast<ElidedLabel*>(m_titleLabel)->setText(m_title);
	m_badgeLabel->setText(m_badge);
	m_badgeLabel->setVisible(!m_badge.isEmpty());
}

QToolButton* SidebarPage::addHeaderButton(const QString& iconName, const QString& text, const QString& toolTip)
{
	QToolButton* button = createHeaderButton(iconName, text, toolTip);
	m_headerLayout->addWidget(button);
	return button;
}

void SidebarPage::addHeaderWidget(QWidget* widget)
{
	if (widget) {
		m_headerLayout->addWidget(widget);
	}
}

void SidebarPage::setScrollable(bool scrollable)
{
	// Every page sits in a resizing scroll area: it fills the page while its
	// sections fit, stretching sections taking the spare height, and scrolls
	// once they do not, rather than squeezing a section's controls. A page
	// that fills is one with a stretching section; the flag records intent.
	m_scrollable = scrollable;
}

bool SidebarPage::isScrollable() const
{
	return m_scrollable;
}

QVBoxLayout* SidebarPage::contentLayout()
{
	return m_contentLayout;
}

void SidebarPage::addWidget(QWidget* widget, int stretch)
{
	if (!widget) {
		return;
	}
	// Before the trailing stretch, which keeps short pages at the top.
	m_contentLayout->insertWidget(m_contentLayout->count() - 1, widget, stretch);
	refreshStretch();
}

SidebarSection* SidebarPage::addSection(const QString& sectionId, const QString& title, QWidget* body, int stretch)
{
	auto* section = new SidebarSection(sectionId, title, body);
	section->setStretch(stretch);
	m_sections.push_back(section);
	m_contentLayout->insertWidget(m_contentLayout->count() - 1, section, stretch);
	connect(section, &SidebarSection::expandedChanged, this, [this, section](bool expanded) {
		refreshStretch();
		emit sectionExpandedChanged(section->sectionId(), expanded);
	});
	refreshStretch();
	return section;
}

void SidebarPage::adoptSection(SidebarSection* section)
{
	if (!section || m_sections.contains(section)) {
		return;
	}
	m_sections.push_back(section);
	connect(section, &SidebarSection::expandedChanged, this, [this, section](bool expanded) {
		emit sectionExpandedChanged(section->sectionId(), expanded);
	});
	connect(section, &QObject::destroyed, this, [this, section]() { m_sections.removeAll(section); });
}

QVector<SidebarSection*> SidebarPage::sections() const
{
	return m_sections;
}

SidebarSection* SidebarPage::section(const QString& sectionId) const
{
	for (SidebarSection* entry : m_sections) {
		if (entry->sectionId() == sectionId) {
			return entry;
		}
	}
	return nullptr;
}

void SidebarPage::refreshStretch()
{
	// An open stretching section takes the spare height; with none open the
	// trailing stretch keeps the sections together at the top.
	bool filled = false;
	for (int index = 0; index < m_contentLayout->count(); ++index) {
		QWidget* widget = m_contentLayout->itemAt(index)->widget();
		if (auto* section = qobject_cast<SidebarSection*>(widget)) {
			const int stretch = section->isExpanded() ? section->stretch() : 0;
			m_contentLayout->setStretch(index, stretch);
			filled = filled || stretch > 0;
		} else if (widget) {
			filled = filled || m_contentLayout->stretch(index) > 0;
		}
	}
	m_contentLayout->setStretch(m_contentLayout->count() - 1, filled ? 0 : 1);
}

} // namespace vibestudio
