#include "app/studio_layout.h"

#include "app/studio_charts.h"
#include "app/studio_icons.h"
#include "app/studio_theme.h"
#include "core/operation_state.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionMenuItem>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QPointer>
#include <QScopedValueRollback>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <functional>

namespace vibestudio {

namespace {

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

// See clearOnEscape(). ShortcutOverride is claimed too, so a surface shortcut
// bound to Escape does not fire while the field still has text to clear.
class EscapeClearsField final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		auto* field = qobject_cast<QLineEdit*>(watched);
		if (field && !field->text().isEmpty()
			&& (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
			const auto* key = static_cast<QKeyEvent*>(event);
			if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
				if (event->type() == QEvent::ShortcutOverride) {
					event->accept();
					return true;
				}
				field->clear();
				return true;
			}
		}
		return QObject::eventFilter(watched, event);
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
	// QLabel's text-dependent height can change by a pixel when a status gains
	// dimensions or bidi isolates. A single-line readout must not resize its
	// neighboring viewport during a gesture. Font/style changes still scale it.
	const int height = fontMetrics().height() + margins.top() + margins.bottom() + 2 * frameWidth();
	return {width, height};
}

QSize ElidedLabel::minimumSizeHint() const
{
	return {fontMetrics().horizontalAdvance(QStringLiteral("...")) * 2, sizeHint().height()};
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

QString railBehaviourId(RailBehaviour behaviour)
{
	switch (behaviour) {
	case RailBehaviour::Expanded:
		return QStringLiteral("expanded");
	case RailBehaviour::Compact:
		return QStringLiteral("compact");
	case RailBehaviour::Automatic:
		break;
	}
	return QStringLiteral("automatic");
}

RailBehaviour railBehaviourFromId(const QString& id)
{
	const QString normalized = id.trimmed().toLower();
	if (normalized == QLatin1String("expanded")) {
		return RailBehaviour::Expanded;
	}
	if (normalized == QLatin1String("compact")) {
		return RailBehaviour::Compact;
	}
	return RailBehaviour::Automatic;
}

QString localizedRailBehaviourName(RailBehaviour behaviour)
{
	switch (behaviour) {
	case RailBehaviour::Expanded:
		return QCoreApplication::translate("VibeStudioLayout", "Always show labels");
	case RailBehaviour::Compact:
		return QCoreApplication::translate("VibeStudioLayout", "Icons only");
	case RailBehaviour::Automatic:
		break;
	}
	return QCoreApplication::translate("VibeStudioLayout", "Collapse to icons automatically");
}

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

	// The pin sits where the icons do, so it stays put as the rail opens.
	auto* toggleRow = new QHBoxLayout;
	toggleRow->setContentsMargins(12, 2, 8, 0);
	m_toggle = new QToolButton;
	m_toggle->setObjectName(QStringLiteral("railToggle"));
	m_toggle->setCheckable(true);
	m_toggle->setIconSize(QSize(16, 16));
	m_toggle->setFocusPolicy(Qt::TabFocus);
	m_toggle->installEventFilter(this);
	connect(m_toggle, &QToolButton::clicked, this, [this]() {
		// Pins the labels open, or lets them fold back the way the user
		// chose to have them folded.
		const RailBehaviour next = m_behaviour == RailBehaviour::Expanded ? m_collapsedBehaviour : RailBehaviour::Expanded;
		setBehaviour(next);
		emit behaviourChanged(next);
	});
	toggleRow->addWidget(m_toggle);
	toggleRow->addStretch(1);
	root->addLayout(toggleRow);

	m_group = new QButtonGroup(this);
	m_group->setExclusive(true);
	// The current page's marker follows the checked entry.
	connect(m_group, &QButtonGroup::idToggled, this, [this](int, bool) {
		update();
	});
	connect(m_group, &QButtonGroup::idClicked, this, [this](int id) {
		emit currentIdChanged(id);
		// A page picked with the pointer is where the user is going, so the
		// labels fold away, and stay folded until the pointer has left: Qt
		// enters the rail again when a dialog closes over it. Picked from the
		// keyboard, the rail keeps them while focus is still in it.
		if (!holdsFocus()) {
			m_openTimer->stop();
			m_waitForLeave = m_hovered;
			if (m_open) {
				setOpen(false);
			}
		}
	});

	m_openTimer = new QTimer(this);
	m_openTimer->setSingleShot(true);
	m_openTimer->setInterval(kOpenDelayMsecs);
	connect(m_openTimer, &QTimer::timeout, this, [this]() {
		if (m_hovered && !m_waitForLeave) {
			setOpen(true);
		}
	});
	m_closeTimer = new QTimer(this);
	m_closeTimer->setSingleShot(true);
	m_closeTimer->setInterval(kCloseDelayMsecs);
	connect(m_closeTimer, &QTimer::timeout, this, &ModeRail::closeUnlessHeld);

	m_animation = new QVariantAnimation(this);
	m_animation->setDuration(150);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
		m_currentWidth = value.toInt();
		emit geometryNeeded();
	});
	connect(m_animation, &QVariantAnimation::finished, this, [this]() {
		// The labels go once the rail has closed over them.
		if (!m_open && effectiveBehaviour() != RailBehaviour::Expanded) {
			showLabels(false);
		}
		emit geometryNeeded();
	});

	refreshPresentation();
}

void ModeRail::setEntries(const QVector<ModeRailEntry>& entries)
{
	m_entries = entries;
	rebuild();
}

void ModeRail::rebuild()
{
	// Hidden now, deleted later: a button waiting for deletion would otherwise
	// still be drawn under its replacement.
	for (QToolButton* button : std::as_const(m_buttons)) {
		m_group->removeButton(button);
		button->hide();
		button->deleteLater();
	}
	m_buttons.clear();
	auto clearLayout = [](QVBoxLayout* layout) {
		while (QLayoutItem* item = layout->takeAt(0)) {
			if (QWidget* widget = item->widget()) {
				widget->hide();
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
		button->setProperty("railLabel", entry.label);
		// One style whether the label shows or not, so the icon never moves
		// as the rail opens: a folded row simply has no text.
		button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		button->setToolTip(QStringLiteral("%1\n%2").arg(entry.label, entry.hint));
		button->setAccessibleName(entry.label);
		button->setAccessibleDescription(entry.hint);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		button->installEventFilter(this);
		m_group->addButton(button, entry.id);
		m_buttons.push_back(button);
		target->addWidget(button);
	}
	m_labelsShown = false;
	refreshPresentation();
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

void ModeRail::setHint(int id, const QString& hint)
{
	for (ModeRailEntry& entry : m_entries) {
		if (entry.id != id) {
			continue;
		}
		entry.hint = hint;
		if (QAbstractButton* button = m_group ? m_group->button(id) : nullptr) {
			button->setToolTip(QStringLiteral("%1\n%2").arg(entry.label, hint));
			button->setAccessibleDescription(hint);
		}
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

void ModeRail::setBehaviour(RailBehaviour behaviour)
{
	if (behaviour != RailBehaviour::Expanded) {
		m_collapsedBehaviour = behaviour;
	}
	m_behaviour = behaviour;
	m_openTimer->stop();
	m_closeTimer->stop();
	m_open = false;
	// Folding the labels under the pointer that asked for it must not open
	// them again straight away.
	m_waitForLeave = m_hovered && effectiveBehaviour() != RailBehaviour::Expanded;
	refreshPresentation(true);
}

RailBehaviour ModeRail::behaviour() const
{
	return m_behaviour;
}

void ModeRail::setAvailableWidth(int width)
{
	const RailBehaviour before = effectiveBehaviour();
	m_availableWidth = width;
	if (effectiveBehaviour() != before) {
		m_open = false;
		refreshPresentation();
	}
}

RailBehaviour ModeRail::effectiveBehaviour() const
{
	// A pinned rail gives way in a narrow window, as an automatic one: the
	// page needs the width more than the labels do.
	if (m_behaviour == RailBehaviour::Expanded && m_availableWidth > 0 && m_availableWidth < expandedWidth() * 6) {
		return RailBehaviour::Automatic;
	}
	return m_behaviour;
}

void ModeRail::setOpen(bool open)
{
	m_openTimer->stop();
	m_closeTimer->stop();
	if (open && effectiveBehaviour() != RailBehaviour::Automatic) {
		return;
	}
	if (m_open == open) {
		return;
	}
	m_open = open;
	if (open) {
		raise();
		showLabels(true);
	}
	refreshToggle();
	animateTo(open ? expandedWidth() : restingWidth());
}

bool ModeRail::isOpen() const
{
	return m_open;
}

bool ModeRail::showsLabels() const
{
	return m_labelsShown;
}

int ModeRail::restingWidth() const
{
	return effectiveBehaviour() == RailBehaviour::Expanded ? expandedWidth() : compactWidth();
}

int ModeRail::currentWidth() const
{
	return m_currentWidth > 0 ? m_currentWidth : restingWidth();
}

int ModeRail::compactWidth() const
{
	// One glyph plus the row's padding either side of it.
	const QSize glyph = scaledIconSize(QSize(20, 20));
	return std::max(kRailCompactWidth, glyph.width() + 32);
}

int ModeRail::expandedWidth() const
{
	// Fits the longest (possibly translated) label at the current text scale.
	const QSize glyph = scaledIconSize(QSize(20, 20));
	const int iconBox = glyph.width() + glyph.width() * 2 / 5;
	QFont bold = m_buttons.isEmpty() ? font() : m_buttons.first()->font();
	bold.setWeight(QFont::DemiBold);
	const QFontMetrics metrics(bold);
	int widestLabel = 0;
	for (const ModeRailEntry& entry : m_entries) {
		widestLabel = std::max(widestLabel, metrics.horizontalAdvance(entry.label));
	}
	return std::max(kRailExpandedWidth, widestLabel + iconBox + 44);
}

void ModeRail::setReducedMotion(bool reduced)
{
	m_reducedMotion = reduced;
}

void ModeRail::refreshPresentation(bool animate)
{
	const QSize glyph = scaledIconSize(QSize(20, 20));
	// The glyph leads a box wider than itself; the rest of the box is the
	// gap before the label.
	const QSize iconBox(glyph.width() + glyph.width() * 2 / 5, glyph.height());
	for (QToolButton* button : std::as_const(m_buttons)) {
		// The current entry's glyph takes the accent.
		button->setIcon(studioIcon(button->property("railIconName").toString(), StudioIconTone::Navigation, StudioIconAlignment::Leading));
		button->setIconSize(iconBox);
	}
	const bool labels = m_open || effectiveBehaviour() == RailBehaviour::Expanded;
	if (labels) {
		showLabels(true);
	}
	refreshToggle();
	const int target = labels ? expandedWidth() : compactWidth();
	if (animate) {
		animateTo(target);
		return;
	}
	m_animation->stop();
	m_currentWidth = target;
	if (!labels) {
		showLabels(false);
	}
	emit geometryNeeded();
}

void ModeRail::refreshToggle()
{
	if (!m_toggle) {
		return;
	}
	const bool pinned = m_behaviour == RailBehaviour::Expanded;
	const QSignalBlocker blocker(m_toggle);
	m_toggle->setChecked(pinned);
	m_toggle->setIconSize(scaledIconSize(QSize(16, 16)));
	m_toggle->setIcon(studioIcon(QStringLiteral("pin"), pinned ? StudioIconTone::Accent : StudioIconTone::Muted));
	// The name stays; the checked state says whether it is on.
	m_toggle->setAccessibleName(tr("Keep navigation open"));
	m_toggle->setToolTip(pinned
			? tr("Labels are pinned open. Click to let the navigation fold back to icons.")
			: tr("Keep navigation open, labels and all, beside the page."));
}

void ModeRail::showLabels(bool shown)
{
	if (m_labelsShown == shown) {
		return;
	}
	m_labelsShown = shown;
	for (QToolButton* button : std::as_const(m_buttons)) {
		button->setText(shown ? button->property("railLabel").toString() : QString());
	}
}

void ModeRail::animateTo(int width)
{
	m_animation->stop();
	if (m_reducedMotion || !isVisible() || m_currentWidth <= 0 || m_currentWidth == width) {
		m_currentWidth = width;
		if (!m_open && effectiveBehaviour() != RailBehaviour::Expanded) {
			showLabels(false);
		}
		emit geometryNeeded();
		return;
	}
	m_animation->setStartValue(m_currentWidth);
	m_animation->setEndValue(width);
	m_animation->start();
}

bool ModeRail::holdsFocus() const
{
	const QWidget* focused = QApplication::focusWidget();
	return focused && (focused == this || isAncestorOf(focused));
}

void ModeRail::closeUnlessHeld()
{
	// Focus keeps the labels open for keyboard users; the pointer resting on
	// the rail keeps them for everyone else.
	if (m_hovered || holdsFocus()) {
		return;
	}
	setOpen(false);
}

void ModeRail::enterEvent(QEnterEvent* event)
{
	m_hovered = true;
	m_closeTimer->stop();
	if (!m_open && !m_waitForLeave && effectiveBehaviour() == RailBehaviour::Automatic) {
		m_openTimer->start();
	}
	QWidget::enterEvent(event);
}

void ModeRail::leaveEvent(QEvent* event)
{
	m_hovered = false;
	m_waitForLeave = false;
	m_openTimer->stop();
	if (m_open) {
		m_closeTimer->start();
	}
	QWidget::leaveEvent(event);
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
	const bool ours = watched == m_toggle || m_buttons.contains(qobject_cast<QToolButton*>(watched));
	if (!ours) {
		return QWidget::eventFilter(watched, event);
	}
	switch (event->type()) {
	case QEvent::FocusIn:
		// Keyboard users see the labels at once; there is no pointer to
		// linger. A click never gives these buttons focus.
		if (effectiveBehaviour() == RailBehaviour::Automatic) {
			setOpen(true);
		}
		break;
	case QEvent::FocusOut:
		// By now the application has moved focus on; the labels fold
		// unless it moved within the rail or the pointer still rests here.
		if (m_open && !m_hovered) {
			m_closeTimer->start();
		}
		break;
	case QEvent::KeyPress: {
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier) {
			emit dismissed();
			if (m_open) {
				m_waitForLeave = m_hovered;
				setOpen(false);
			}
			return true;
		}
		if (watched == m_toggle) {
			break;
		}
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
		break;
	}
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}

void ModeRail::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
		update();
	}
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		refreshPresentation();
	}
	QWidget::changeEvent(event);
}

void ModeRail::paintEvent(QPaintEvent* event)
{
	QWidget::paintEvent(event);
	const QAbstractButton* current = m_group ? m_group->checkedButton() : nullptr;
	if (!current || !current->isVisible()) {
		return;
	}
	// A short bar, not the whole row: it marks the page without boxing it in,
	// and sits in the gap the highlighted entry leaves at the rail's edge.
	const QRect row = current->geometry();
	const int barHeight = std::max(10, row.height() - 18);
	const int barWidth = 3;
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	const QRectF bar(mirrored ? width() - barWidth - 1 : 1, row.center().y() - barHeight / 2.0 + 0.5, barWidth, barHeight);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(currentStudioTheme().colors.accent);
	painter.drawRoundedRect(bar, barWidth / 2.0, barWidth / 2.0);
}

// ---------------------------------------------------------------------------
// RailHost
// ---------------------------------------------------------------------------

namespace {

// The soft edge beside an open rail, telling the page's edge from the rail
// lying over it. High-contrast themes draw a solid line instead.
class RailShade final : public QWidget {
public:
	explicit RailShade(QWidget* parent)
		: QWidget(parent)
	{
		setObjectName(QStringLiteral("railShade"));
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
		setAttribute(Qt::WA_NoSystemBackground, true);
		setFocusPolicy(Qt::NoFocus);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		const StudioThemeTokens& tokens = currentStudioTheme();
		const bool mirrored = layoutDirection() == Qt::RightToLeft;
		if (tokens.highContrast) {
			painter.fillRect(QRect(mirrored ? width() - 2 : 0, 0, 2, height()), tokens.colors.border);
			return;
		}
		QLinearGradient shade(mirrored ? QPointF(width(), 0) : QPointF(0, 0), mirrored ? QPointF(0, 0) : QPointF(width(), 0));
		QColor dark(0, 0, 0, tokens.light ? 46 : 110);
		shade.setColorAt(0.0, dark);
		dark.setAlpha(0);
		shade.setColorAt(1.0, dark);
		painter.fillRect(rect(), shade);
	}
};

} // namespace

RailHost::RailHost(ModeRail* rail, QWidget* content, QWidget* parent)
	: QWidget(parent)
	, m_rail(rail)
	, m_content(content)
{
	setObjectName(QStringLiteral("studioRoot"));
	// The page's layout slot is the rail's resting width. The rail itself is
	// placed by hand over that slot, free to grow past it.
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	m_footprint = new QWidget;
	m_footprint->setObjectName(QStringLiteral("railFootprint"));
	m_footprint->setFocusPolicy(Qt::NoFocus);
	layout->addWidget(m_footprint);
	layout->addWidget(m_content, 1);

	m_shade = new RailShade(this);
	m_shade->hide();
	m_rail->setParent(this);
	m_rail->raise();
	connect(m_rail, &ModeRail::geometryNeeded, this, [this]() {
		placeRail();
	});
	placeRail();
}

bool RailHost::event(QEvent* event)
{
	if (event->type() == QEvent::LayoutDirectionChange) {
		placeRail();
	}
	return QWidget::event(event);
}

void RailHost::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	m_rail->setAvailableWidth(width());
	placeRail();
}

void RailHost::placeRail()
{
	const int resting = m_rail->restingWidth();
	if (m_footprint->width() != resting || m_footprint->minimumWidth() != resting) {
		m_footprint->setFixedWidth(resting);
		// The rail's height still counts toward the window's minimum size.
		m_footprint->setMinimumHeight(m_rail->minimumSizeHint().height());
	}
	const int current = std::max(resting, m_rail->currentWidth());
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	const int x = mirrored ? width() - current : 0;
	m_rail->setGeometry(x, 0, current, height());
	m_rail->raise();
	const bool floating = current > resting;
	if (floating) {
		constexpr int kShadeWidth = 12;
		m_shade->setGeometry(mirrored ? x - kShadeWidth : current, 0, kShadeWidth, height());
		m_shade->raise();
	}
	m_shade->setVisible(floating);
}

// ---------------------------------------------------------------------------
// PageTransition
// ---------------------------------------------------------------------------

PageTransition::PageTransition(QWidget* host)
	: QWidget(host)
	, m_host(host)
{
	setObjectName(QStringLiteral("pageTransition"));
	setAttribute(Qt::WA_TransparentForMouseEvents, true);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setFocusPolicy(Qt::NoFocus);
	hide();
	m_animation = new QVariantAnimation(this);
	m_animation->setDuration(kDurationMsecs);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
		m_strength = value.toReal();
		update();
	});
	connect(m_animation, &QVariantAnimation::finished, this, [this]() {
		m_strength = 0.0;
		hide();
	});
	if (m_host) {
		m_host->installEventFilter(this);
	}
}

void PageTransition::setReducedMotion(bool reduced)
{
	m_reducedMotion = reduced;
	if (reduced && m_animation->state() == QAbstractAnimation::Running) {
		m_animation->stop();
		m_strength = 0.0;
		hide();
	}
}

void PageTransition::play()
{
	// Offscreen and minimal platforms have nobody to show a fade to; tests
	// and snapshots must see the page itself straight away.
	const QString platform = QGuiApplication::platformName();
	if (m_reducedMotion || !m_host || !m_host->isVisible() || platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
		return;
	}
	m_animation->stop();
	setGeometry(m_host->rect());
	raise();
	show();
	m_animation->setStartValue(0.55);
	m_animation->setEndValue(0.0);
	m_animation->start();
}

bool PageTransition::isPlaying() const
{
	return m_animation->state() == QAbstractAnimation::Running;
}

void PageTransition::paintEvent(QPaintEvent*)
{
	if (m_strength <= 0.0) {
		return;
	}
	// The page comes up out of the surface colour it sits on.
	QColor veil = currentStudioTheme().colors.surface;
	veil.setAlphaF(static_cast<float>(std::clamp(m_strength, 0.0, 1.0)));
	QPainter painter(this);
	painter.fillRect(rect(), veil);
}

bool PageTransition::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_host && event->type() == QEvent::Resize && isVisible()) {
		setGeometry(m_host->rect());
	}
	return QWidget::eventFilter(watched, event);
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
	setAccessibleName(QCoreApplication::translate("VibeStudioLayout", "Page header"));

	// One row: the page's glyph and name, then what it is showing, then its
	// state and actions. The rail already says where the user is, so the
	// header stays a slim context bar rather than a two-line banner.
	auto* root = new QHBoxLayout(this);
	root->setContentsMargins(14, 7, 12, 7);
	root->setSpacing(10);

	m_icon = new QLabel;
	m_icon->setObjectName(QStringLiteral("pageIcon"));
	m_icon->setFixedSize(22, 22);
	m_icon->setAlignment(Qt::AlignCenter);
	m_icon->setAccessibleName(QString());
	root->addWidget(m_icon, 0, Qt::AlignVCenter);

	m_title = new QLabel(title);
	m_title->setObjectName(QStringLiteral("pageTitle"));
	m_title->setAccessibleName(QCoreApplication::translate("VibeStudioLayout", "Page title"));
	root->addWidget(m_title, 0, Qt::AlignVCenter);
	m_divider = new QFrame;
	m_divider->setObjectName(QStringLiteral("pageHeaderDivider"));
	m_divider->setFrameShape(QFrame::NoFrame);
	m_divider->setAttribute(Qt::WA_StyledBackground, true);
	m_divider->setFixedSize(1, 16);
	m_divider->setVisible(false);
	root->addWidget(m_divider, 0, Qt::AlignVCenter);
	m_subtitle = new ElidedLabel;
	m_subtitle->setObjectName(QStringLiteral("pageSubtitle"));
	m_subtitle->setAccessibleName(QCoreApplication::translate("VibeStudioLayout", "Page context"));
	m_subtitle->setElideMode(Qt::ElideMiddle);
	m_subtitle->setVisible(false);
	root->addWidget(m_subtitle, 1, Qt::AlignVCenter);
	// Without a context line the title keeps the free space to itself.
	m_titleStretch = new QWidget;
	m_titleStretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_titleStretch->setFixedHeight(1);
	root->addWidget(m_titleStretch, 1);

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
	const bool shown = !subtitle.trimmed().isEmpty();
	m_subtitle->setVisible(shown);
	m_divider->setVisible(shown);
	m_titleStretch->setVisible(!shown);
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
	if (m_primaryMuted && widget && widget->property("variant").toString() == QStringLiteral("primary")) {
		widget->setProperty("headerPrimary", true);
		setButtonVariant(widget, QString());
	}
}

void PageHeader::setPrimaryActionMuted(bool muted)
{
	if (m_primaryMuted == muted) {
		return;
	}
	m_primaryMuted = muted;
	for (int index = 0; index < m_actionLayout->count(); ++index) {
		QWidget* widget = m_actionLayout->itemAt(index)->widget();
		if (!qobject_cast<QPushButton*>(widget)) {
			continue;
		}
		if (muted && widget->property("variant").toString() == QStringLiteral("primary")) {
			widget->setProperty("headerPrimary", true);
			setButtonVariant(widget, QString());
		} else if (!muted && widget->property("headerPrimary").toBool()) {
			widget->setProperty("headerPrimary", QVariant());
			setButtonVariant(widget, QStringLiteral("primary"));
		}
	}
	fitActions();
}

bool PageHeader::primaryActionMuted() const
{
	return m_primaryMuted;
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
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
		fitActions();
	}
}

namespace {

// A header action's whole label, learned from the button while it shows one,
// so a label the page changes later is the one folding keeps.
QString actionLabel(QPushButton* button)
{
	if (!button->text().isEmpty()) {
		button->setProperty("actionLabel", button->text());
	}
	return button->property("actionLabel").toString();
}

// The width a button takes showing only its glyph.
int foldedActionWidth(const QPushButton* button)
{
	QStyleOptionButton option;
	option.initFrom(button);
	option.icon = button->icon();
	option.iconSize = button->iconSize();
	return button->style()->sizeFromContents(QStyle::CT_PushButton, &option, button->iconSize(), button).width();
}

void setActionFolded(QPushButton* button, bool folded)
{
	const QString label = actionLabel(button);
	if (button->text() != (folded ? QString() : label)) {
		button->setText(folded ? QString() : label);
	}
	// Folded, the label lives on in the tooltip, before what the tooltip said.
	if (folded) {
		if (!button->property("tipBeforeFold").isValid()) {
			button->setProperty("tipBeforeFold", button->toolTip());
		}
		const QString tip = button->property("tipBeforeFold").toString();
		const QString name = withoutMnemonic(label);
		button->setToolTip(tip.isEmpty() || tip == name ? name : QStringLiteral("%1\n%2").arg(name, tip));
	} else if (button->property("tipBeforeFold").isValid()) {
		button->setToolTip(button->property("tipBeforeFold").toString());
		button->setProperty("tipBeforeFold", QVariant());
	}
	if (button->accessibleName().isEmpty()) {
		button->setAccessibleName(withoutMnemonic(label));
	}
}

} // namespace

QVector<QPushButton*> PageHeader::foldableActions() const
{
	QVector<QPushButton*> buttons;
	for (int index = 0; index < m_actionLayout->count(); ++index) {
		// Only a button with a glyph has something to fold to.
		auto* button = qobject_cast<QPushButton*>(m_actionLayout->itemAt(index)->widget());
		if (button && !button->icon().isNull() && !button->isHidden()) {
			buttons << button;
		}
	}
	return buttons;
}

QSize PageHeader::minimumSizeHint() const
{
	QSize hint = QWidget::minimumSizeHint();
	for (QPushButton* button : foldableActions()) {
		if (!button->text().isEmpty()) {
			hint.rwidth() -= std::max(0, button->minimumSizeHint().width() - foldedActionWidth(button));
		}
	}
	return hint;
}

void PageHeader::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	fitActions();
}

void PageHeader::fitActions()
{
	if (m_fitting || !layout()) {
		return;
	}
	m_fitting = true;
	const QVector<QPushButton*> buttons = foldableActions();
	const auto fits = [this]() {
		m_actionLayout->invalidate();
		layout()->invalidate();
		return layout()->minimumSize().width() <= width();
	};
	// Every label if they fit; else the secondary actions fold, then the
	// primary one too.
	for (QPushButton* button : buttons) {
		setActionFolded(button, false);
	}
	if (!fits()) {
		for (QPushButton* button : buttons) {
			if (button->property("variant").toString() != QStringLiteral("primary")) {
				setActionFolded(button, true);
			}
		}
		if (!fits()) {
			for (QPushButton* button : buttons) {
				setActionFolded(button, true);
			}
		}
	}
	m_fitting = false;
}

void PageHeader::refreshIcon()
{
	if (!m_icon) {
		return;
	}
	// Every page's header stands as tall as one with buttons, so switching
	// pages never makes the work area below jump.
	const StudioThemeMetrics& metrics = currentStudioTheme().metrics;
	const int buttonHeight = std::max(fontMetrics().height(), metrics.controlHeight - 2 * metrics.controlPaddingVertical - 2 * metrics.borderWidth)
		+ 2 * metrics.controlPaddingVertical + 2 * metrics.borderWidth;
	if (QLayout* row = layout()) {
		const QMargins margins = row->contentsMargins();
		setMinimumHeight(buttonHeight + margins.top() + margins.bottom() + 1);
	}
	const int side = scaledIconSize(QSize(18, 18)).width();
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
	m_column = layout;

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
	fitActions();
}

void EmptyStateView::setBody(const QString& body)
{
	m_body->setText(body);
	setAccessibleDescription(body);
	fitActions();
}

void EmptyStateView::setDetail(QWidget* widget)
{
	if (m_detail) {
		m_column->removeWidget(m_detail);
		m_detail->deleteLater();
	}
	m_detail = widget;
	if (widget) {
		m_column->addWidget(widget);
	}
}

QPushButton* EmptyStateView::addAction(const QString& text, const QString& iconName, bool primary)
{
	QPushButton* button = createButton(text, iconName, primary ? QStringLiteral("primary") : QString());
	// Insert before the trailing stretch so the buttons stay centred.
	m_actions->insertWidget(m_actions->count() - 1, button);
	fitActions();
	return button;
}

void EmptyStateView::fitActions()
{
	if (!m_actions) { return; }
	int needed = 0, count = 0;
	for (int i = 0; i < m_actions->count(); ++i) {
		if (auto* widget = m_actions->itemAt(i)->widget()) { needed += widget->sizeHint().width(); ++count; }
	}
	needed += std::max(0, count - 1) * m_actions->spacing();
	const int columnWidth = std::min(480, std::max(1, width() - 48));
	m_actions->setDirection(needed > columnWidth ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
	// Give wrapping labels their full height, including inside a scroll area.
	for (auto* label : {m_title, m_body}) { label->setMinimumHeight(std::max(0, label->heightForWidth(columnWidth))); }
}

void EmptyStateView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	fitActions();
}

void EmptyStateView::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
		refreshIcon();
	}
	QWidget::changeEvent(event);
	if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) { fitActions(); }
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
// NoticeBar
// ---------------------------------------------------------------------------

namespace {

// The state edge is painted (see NoticeBar::paintEvent()) because a style
// sheet border-left stays on the left in a right-to-left layout. The leading
// margin clears it.
constexpr int kNoticeEdgeWidth = 3;
constexpr int kNoticeLeadingMargin = kNoticeEdgeWidth + 14;
constexpr int kNoticeTrailingMargin = 6;
constexpr int kNoticeVerticalMargin = 6;

} // namespace

NoticeBar::NoticeBar(QWidget* parent)
	: QFrame(parent)
{
	setObjectName(QStringLiteral("noticeBar"));
	setAttribute(Qt::WA_StyledBackground, true);
	setAccessibleName(QCoreApplication::translate("VibeStudioLayout", "Notice"));
	auto* layout = new QHBoxLayout(this);
	layout->setSpacing(10);
	refreshMargins();
	m_icon = new QLabel;
	m_icon->setObjectName(QStringLiteral("noticeIcon"));
	layout->addWidget(m_icon);
	m_title = new QLabel;
	m_title->setObjectName(QStringLiteral("noticeTitle"));
	layout->addWidget(m_title);
	// The message wraps rather than elides: a notice is read once, in full.
	m_text = new QLabel;
	m_text->setObjectName(QStringLiteral("noticeText"));
	m_text->setWordWrap(true);
	m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	layout->addWidget(m_text, 1);
	m_actions = new QHBoxLayout;
	m_actions->setSpacing(6);
	layout->addLayout(m_actions);
	m_close = createToolButton(QStringLiteral("close"), QString(), QCoreApplication::translate("VibeStudioLayout", "Dismiss this notice (Esc)"), false);
	m_close->setObjectName(QStringLiteral("noticeClose"));
	m_close->setAccessibleName(QCoreApplication::translate("VibeStudioLayout", "Dismiss notice"));
	setBaseIconSize(m_close, QSize(14, 14));
	layout->addWidget(m_close);
	connect(m_close, &QToolButton::clicked, this, &NoticeBar::dismiss);
	hide();
}

void NoticeBar::showNotice(const QString& operationStateId, const QString& title, const QString& text)
{
	// A notice replaced is a notice dismissed, for whoever waits on it.
	if (!isHidden()) {
		emit dismissed();
	}
	clearActions();
	m_stateId = operationStateId;
	setProperty("operationState", operationStateId);
	if (QStyle* current = style()) {
		current->unpolish(this);
		current->polish(this);
	}
	m_title->setText(title);
	m_text->setText(text);
	setAccessibleDescription(QStringLiteral("%1 %2").arg(title, text));
	refreshIcon();
	// The edge takes the new state's colour (see paintEvent()).
	update();
	show();
	// Assistive technology hears the notice without the bar taking focus.
	QAccessibleEvent alert(this, QAccessible::Alert);
	QAccessible::updateAccessibility(&alert);
}

QPushButton* NoticeBar::addAction(const QString& text, const QString& iconName, bool primary)
{
	QPushButton* button = createButton(text, iconName, primary ? QStringLiteral("primary") : QString());
	// Tab reaches the actions in the order they are drawn, then the close
	// button; created late, the button would otherwise come last in the window.
	QWidget* before = m_actions->count() > 0 ? m_actions->itemAt(m_actions->count() - 1)->widget() : m_text;
	m_actions->addWidget(button);
	QWidget::setTabOrder(before, button);
	QWidget::setTabOrder(button, m_close);
	return button;
}

void NoticeBar::dismiss()
{
	hide();
	clearActions();
	emit dismissed();
}

QString NoticeBar::title() const
{
	return m_title->text();
}

QString NoticeBar::text() const
{
	return m_text->text();
}

QString NoticeBar::stateId() const
{
	return m_stateId;
}

void NoticeBar::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
		dismiss();
		event->accept();
		return;
	}
	QFrame::keyPressEvent(event);
}

void NoticeBar::changeEvent(QEvent* event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
		refreshIcon();
	}
	if (event->type() == QEvent::LayoutDirectionChange) {
		refreshMargins();
		update();
	}
	QFrame::changeEvent(event);
}

void NoticeBar::paintEvent(QPaintEvent* event)
{
	QFrame::paintEvent(event);
	// The state's colour on the leading edge, as LoadingPane paints its own;
	// the title names the state in words.
	const StudioThemeTokens& theme = currentStudioTheme();
	QColor edge = studioThemeStateColor(theme, m_stateId);
	if (!edge.isValid()) {
		edge = theme.colors.borderStrong;
	}
	QPainter painter(this);
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	painter.fillRect(QRect(mirrored ? width() - kNoticeEdgeWidth : 0, 0, kNoticeEdgeWidth, height()), edge);
}

void NoticeBar::refreshMargins()
{
	// Layout margins do not mirror, so the wide one follows the edge.
	QLayout* box = layout();
	if (!box) {
		return;
	}
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	box->setContentsMargins(mirrored ? kNoticeTrailingMargin : kNoticeLeadingMargin, kNoticeVerticalMargin,
		mirrored ? kNoticeLeadingMargin : kNoticeTrailingMargin, kNoticeVerticalMargin);
}

void NoticeBar::clearActions()
{
	// Later, not now: the action being clicked may be the one that asked.
	while (QLayoutItem* item = m_actions->takeAt(0)) {
		if (QWidget* widget = item->widget()) {
			widget->hide();
			widget->deleteLater();
		}
		delete item;
	}
}

void NoticeBar::refreshIcon()
{
	if (!m_icon) {
		return;
	}
	QString name = QStringLiteral("info");
	if (m_stateId == QStringLiteral("failed")) {
		name = QStringLiteral("error");
	} else if (m_stateId == QStringLiteral("warning") || m_stateId == QStringLiteral("cancelled")) {
		name = QStringLiteral("warning");
	} else if (m_stateId == QStringLiteral("completed")) {
		name = QStringLiteral("success");
	}
	QColor color = studioThemeStateColor(currentStudioTheme(), m_stateId);
	if (!color.isValid()) {
		color = currentStudioTheme().colors.info;
	}
	const int side = scaledIconSize(QSize(18, 18)).width();
	m_icon->setPixmap(studioIconPixmap(name, side, devicePixelRatioF(), color));
}

// ---------------------------------------------------------------------------
// ReflowGrid
// ---------------------------------------------------------------------------

ReflowGrid::ReflowGrid(int maximumColumns, QWidget* parent)
	: QWidget(parent)
	, m_maximumColumns(std::max(1, maximumColumns))
{
	m_grid = new QGridLayout(this);
	m_grid->setContentsMargins(0, 0, 0, 0);
	m_grid->setHorizontalSpacing(10);
	m_grid->setVerticalSpacing(10);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ReflowGrid::addItem(QWidget* widget)
{
	m_items.push_back(widget);
	m_columns = 0;
	reflow(columnsFor(width() > 0 ? width() : INT_MAX));
}

void ReflowGrid::setSpacing(int spacing)
{
	m_grid->setHorizontalSpacing(spacing);
	m_grid->setVerticalSpacing(spacing);
	m_columns = 0;
	reflow(columnsFor(width() > 0 ? width() : INT_MAX));
}

void ReflowGrid::setColumnStretches(const QVector<int>& stretches)
{
	m_stretches = stretches;
	m_columns = 0;
	reflow(columnsFor(width() > 0 ? width() : INT_MAX));
}

int ReflowGrid::columns() const
{
	return m_columns;
}

void ReflowGrid::setColumnWidth(int width)
{
	m_columnWidth = std::max(0, width);
	m_columns = 0;
	reflow(columnsFor(this->width() > 0 ? this->width() : INT_MAX));
}

int ReflowGrid::columnsFor(int width) const
{
	// What a column wants decides how many fit: the width given, or else each
	// item's size hint (or more, when it was given a minimum).
	int widest = std::max(1, m_columnWidth);
	if (m_columnWidth <= 0) {
		for (const QWidget* item : m_items) {
			widest = std::max(widest, std::max(item->sizeHint().width(), item->minimumWidth()));
		}
	}
	const int spacing = m_grid->horizontalSpacing();
	const int fit = (width + spacing) / (widest + spacing);
	return std::clamp(fit, 1, std::max(1, std::min(m_maximumColumns, static_cast<int>(m_items.size()))));
}

void ReflowGrid::reflow(int columns)
{
	if (columns == m_columns) {
		return;
	}
	m_columns = columns;
	for (QWidget* item : std::as_const(m_items)) {
		m_grid->removeWidget(item);
	}
	const bool weighted = columns == m_stretches.size();
	for (int column = 0; column < m_maximumColumns; ++column) {
		m_grid->setColumnStretch(column, column < columns ? (weighted ? m_stretches.at(column) : 1) : 0);
	}
	for (int index = 0; index < m_items.size(); ++index) {
		m_grid->addWidget(m_items.at(index), index / columns, index % columns);
	}
	updateGeometry();
}

QSize ReflowGrid::minimumSizeHint() const
{
	// One column of the narrowest the items can go is the least it needs;
	// more rows come with it.
	int widest = 0;
	for (const QWidget* item : m_items) {
		widest = std::max(widest, std::max(item->minimumSizeHint().width(), item->minimumWidth()));
	}
	return {widest, m_grid->minimumSize().height()};
}

void ReflowGrid::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	reflow(columnsFor(width()));
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
// NavigationTile
// ---------------------------------------------------------------------------

namespace {

constexpr int kTilePadding = 12;
constexpr int kTileGap = 12;

QColor blendColor(const QColor& from, const QColor& to, double amount)
{
	const double t = std::clamp(amount, 0.0, 1.0);
	return QColor::fromRgbF(static_cast<float>(from.redF() + (to.redF() - from.redF()) * t),
		static_cast<float>(from.greenF() + (to.greenF() - from.greenF()) * t),
		static_cast<float>(from.blueF() + (to.blueF() - from.blueF()) * t), 1.0f);
}

} // namespace

NavigationTile::NavigationTile(const QString& iconName, const QString& title, QWidget* parent)
	: QAbstractButton(parent)
	, m_iconName(iconName)
{
	setObjectName(QStringLiteral("navigationTile"));
	setText(title);
	setFocusPolicy(Qt::TabFocus);
	setAttribute(Qt::WA_Hover, true);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void NavigationTile::setDetail(const QString& detail)
{
	if (m_detail == detail) {
		return;
	}
	m_detail = detail;
	setAccessibleDescription(detail);
	if (m_baseToolTip.isNull()) {
		m_baseToolTip = toolTip();
	}
	setToolTip(detail.isEmpty() ? m_baseToolTip : (m_baseToolTip.isEmpty() ? detail : QStringLiteral("%1\n%2").arg(detail, m_baseToolTip)));
	update();
}

QString NavigationTile::detail() const
{
	return m_detail;
}

QFont NavigationTile::titleFont() const
{
	QFont bold = font();
	bold.setWeight(QFont::DemiBold);
	return bold;
}

int NavigationTile::wellSide() const
{
	return scaledIconSize(QSize(34, 34)).width();
}

QSize NavigationTile::sizeHint() const
{
	const QFontMetrics title(titleFont());
	const QFontMetrics body(font());
	const int textHeight = title.height() + 2 + body.height();
	const int height = std::max(wellSide(), textHeight) + 2 * kTilePadding;
	const int width = 2 * kTilePadding + wellSide() + kTileGap + std::max(title.horizontalAdvance(text()), body.averageCharWidth() * 18);
	return {width, height};
}

QSize NavigationTile::minimumSizeHint() const
{
	const QSize hint = sizeHint();
	const QFontMetrics title(titleFont());
	return {2 * kTilePadding + wellSide() + kTileGap + title.averageCharWidth() * 6, hint.height()};
}

bool NavigationTile::event(QEvent* event)
{
	switch (event->type()) {
	case QEvent::HoverEnter:
	case QEvent::HoverLeave:
	case QEvent::FocusIn:
	case QEvent::FocusOut:
	case QEvent::PaletteChange:
	case QEvent::StyleChange:
	case QEvent::FontChange:
		update();
		if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
			updateGeometry();
		}
		break;
	default:
		break;
	}
	return QAbstractButton::event(event);
}

void NavigationTile::paintEvent(QPaintEvent*)
{
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	const bool hovered = underMouse() && isEnabled();
	const bool pressed = isDown();
	const bool focused = hasFocus();
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	// The card: a quiet surface that lifts a step on hover and sinks a step
	// when pressed; its outline shows keyboard focus.
	const qreal radius = theme.metrics.radius;
	const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	QColor fill = c.panel;
	if (!theme.highContrast) {
		if (pressed) {
			fill = blendColor(c.panel, c.appBackground, 0.5);
		} else if (hovered) {
			fill = blendColor(c.panel, c.panelRaised, 0.7);
		}
	} else if (hovered || pressed) {
		fill = c.panelRaised;
	}
	QColor outline = theme.highContrast ? c.border : (hovered ? c.borderStrong : c.borderSubtle);
	qreal outlineWidth = theme.highContrast ? 2.0 : 1.0;
	if (focused) {
		outline = c.focus;
		outlineWidth = std::max<qreal>(2.0, theme.metrics.focusWidth);
	}
	painter.setPen(QPen(outline, outlineWidth));
	painter.setBrush(fill);
	painter.drawRoundedRect(card.adjusted(outlineWidth / 2 - 0.5, outlineWidth / 2 - 0.5, -(outlineWidth / 2 - 0.5), -(outlineWidth / 2 - 0.5)), radius, radius);

	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	const int side = wellSide();
	const QRect content = rect().adjusted(kTilePadding, kTilePadding, -kTilePadding, -kTilePadding);
	QRect well(0, 0, side, side);
	well.moveTop(rect().center().y() - side / 2 + 1);
	well.moveLeft(mirrored ? content.right() + 1 - side : content.left());
	// The glyph sits in a tinted well, the accent's quiet companion.
	painter.setPen(theme.highContrast ? QPen(c.accent, 1.5) : Qt::NoPen);
	painter.setBrush(theme.highContrast ? QColor(c.panel) : c.accentSubtle);
	painter.drawRoundedRect(QRectF(well).adjusted(0.5, 0.5, -0.5, -0.5), radius - 2, radius - 2);
	const int glyph = side * 3 / 5;
	const QPixmap icon = studioIconPixmap(m_iconName, glyph, devicePixelRatioF(), isEnabled() ? c.accent : c.textFaint);
	painter.drawPixmap(well.center().x() - glyph / 2 + 1, well.center().y() - glyph / 2 + 1, icon);

	const int textLeft = mirrored ? content.left() : well.right() + 1 + kTileGap;
	const int textRight = mirrored ? well.left() - kTileGap : content.right() + 1;
	const int textWidth = std::max(0, textRight - textLeft);
	const QFont bold = titleFont();
	const QFontMetrics title(bold);
	const QFontMetrics body(font());
	const bool hasDetail = !m_detail.isEmpty();
	const int blockHeight = title.height() + (hasDetail ? 2 + body.height() : 0);
	const int top = rect().center().y() - blockHeight / 2 + 1;
	const Qt::Alignment align = Qt::AlignVCenter | (mirrored ? Qt::AlignRight : Qt::AlignLeft);
	painter.setFont(bold);
	painter.setPen(isEnabled() ? c.text : c.textFaint);
	painter.drawText(QRect(textLeft, top, textWidth, title.height()), align, title.elidedText(text(), Qt::ElideRight, textWidth));
	if (hasDetail) {
		painter.setFont(font());
		painter.setPen(isEnabled() ? c.textMuted : c.textFaint);
		painter.drawText(QRect(textLeft, top + title.height() + 2, textWidth, body.height()), align, body.elidedText(m_detail, Qt::ElideMiddle, textWidth));
	}
}

// ---------------------------------------------------------------------------
// Key caps and floating panels
// ---------------------------------------------------------------------------

namespace {

constexpr int kCapPadding = 5;
constexpr int kCapGap = 3;
constexpr int kChordGap = 9;

// "Ctrl+Shift+P, Ctrl+K" as chords of keys; a "+" key keeps its cap.
QVector<QStringList> keyChords(const QString& shortcut)
{
	QVector<QStringList> chords;
	for (const QString& chord : shortcut.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
		QStringList keys;
		QString current;
		for (const QChar character : chord) {
			if (character == QLatin1Char('+') && !current.isEmpty()) {
				keys << current;
				current.clear();
			} else {
				current += character;
			}
		}
		if (!current.isEmpty()) {
			keys << current;
		}
		if (!keys.isEmpty()) {
			chords << keys;
		}
	}
	return chords;
}

int keyCapWidth(const QFontMetrics& metrics, const QString& key)
{
	return std::max(metrics.horizontalAdvance(key) + 2 * kCapPadding, metrics.height() + 2);
}

} // namespace

int keyCapsWidth(const QString& shortcut, const QFont& font)
{
	const QFontMetrics metrics(font);
	const QVector<QStringList> chords = keyChords(shortcut);
	int width = 0;
	for (int chord = 0; chord < chords.size(); ++chord) {
		width += chord > 0 ? kChordGap : 0;
		for (int key = 0; key < chords.at(chord).size(); ++key) {
			width += (key > 0 ? kCapGap : 0) + keyCapWidth(metrics, chords.at(chord).at(key));
		}
	}
	return width;
}

int paintKeyCaps(QPainter* painter, const QRect& area, const QString& shortcut, const QFont& font, Qt::LayoutDirection direction,
	bool enabled, bool onSelection)
{
	const int total = keyCapsWidth(shortcut, font);
	if (!painter || total <= 0) {
		return total;
	}
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	QColor border = theme.highContrast ? c.border : c.borderSubtle;
	QColor fill = theme.highContrast ? c.appBackground : blendColor(c.input, c.text, theme.light ? 0.05 : 0.08);
	QColor ink = theme.highContrast ? c.text : c.textMuted;
	if (onSelection) {
		border = theme.highContrast ? c.selectionText : blendColor(c.rowSelection, c.selectionText, 0.35);
		fill = theme.highContrast ? c.rowSelection : blendColor(c.rowSelection, c.selectionText, 0.10);
		ink = c.selectionText;
	}
	if (!enabled) {
		ink = c.textFaint;
	}
	const QFontMetrics metrics(font);
	const int capHeight = std::min(area.height(), metrics.height() + 4);
	const int top = area.center().y() - capHeight / 2 + 1;
	// Key names read left to right in every language; a right-to-left row
	// puts the block at its own trailing end, the left.
	int x = direction == Qt::RightToLeft ? area.left() : area.right() + 1 - total;
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setFont(font);
	const QVector<QStringList> chords = keyChords(shortcut);
	for (int chord = 0; chord < chords.size(); ++chord) {
		x += chord > 0 ? kChordGap : 0;
		for (int key = 0; key < chords.at(chord).size(); ++key) {
			x += key > 0 ? kCapGap : 0;
			const QString& name = chords.at(chord).at(key);
			const QRectF cap(x, top, keyCapWidth(metrics, name), capHeight);
			painter->setPen(QPen(border, theme.highContrast ? 1.5 : 1.0));
			painter->setBrush(fill);
			painter->drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
			painter->setPen(ink);
			painter->drawText(cap.toRect(), Qt::AlignCenter, name);
			x += static_cast<int>(cap.width());
		}
	}
	painter->restore();
	return total;
}

int prepareFloatingPanel(QWidget* dialog)
{
	if (!dialog) {
		return 0;
	}
	// Rounded corners and a shadow need a window system that composites
	// translucent windows. X11 does only under a compositing manager, which
	// cannot be relied on, so there the panel stays square. Offscreen renders
	// (documentation snapshots, tests) keep the alpha a window system would
	// composite, so they show the panel as users see it.
	const QString platform = QGuiApplication::platformName();
	const bool composited = platform == QLatin1String("windows") || platform == QLatin1String("cocoa") || platform.startsWith(QLatin1String("wayland"))
		|| platform == QLatin1String("offscreen");
	if (!composited) {
		return 0;
	}
	dialog->setAttribute(Qt::WA_TranslucentBackground, true);
	dialog->setProperty("floatingPanel", true);
	return 18;
}

void paintFloatingPanel(QWidget* dialog, int shadowMargin)
{
	if (!dialog || shadowMargin <= 0) {
		return;
	}
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	QPainter painter(dialog);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QRectF panel = QRectF(dialog->rect()).adjusted(shadowMargin, shadowMargin, -shadowMargin, -shadowMargin);
	const qreal radius = theme.metrics.radius + 2;
	// A soft shadow, a little below the panel: rings that build up towards
	// its edge. The high-visibility themes rely on the outline instead.
	if (!theme.highContrast) {
		painter.setPen(Qt::NoPen);
		const int step = theme.light ? 3 : 7;
		for (int ring = shadowMargin; ring > 0; --ring) {
			painter.setBrush(QColor(0, 0, 0, step));
			painter.drawRoundedRect(panel.adjusted(-ring, -ring + 4, ring, ring + 4), radius + ring, radius + ring);
		}
	}
	painter.setPen(QPen(theme.highContrast ? c.border : c.borderStrong, theme.highContrast ? 2.0 : 1.0));
	painter.setBrush(c.panel);
	painter.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

// ---------------------------------------------------------------------------
// StudioMenuBar
// ---------------------------------------------------------------------------

int StudioMenuBar::naturalWidth() const
{
	ensurePolished();
	// Laid out as QMenuBar lays its items out: the panel frame and margin at
	// either end, and each item's styled size with the item spacing after it.
	const int spacing = style()->pixelMetric(QStyle::PM_MenuBarItemSpacing, nullptr, this);
	const int margin = style()->pixelMetric(QStyle::PM_MenuBarHMargin, nullptr, this);
	const int panel = style()->pixelMetric(QStyle::PM_MenuBarPanelWidth, nullptr, this);
	int width = 2 * (panel + margin) + spacing;
	for (QAction* action : actions()) {
		if (!action->isVisible()) {
			continue;
		}
		QStyleOptionMenuItem option;
		initStyleOption(&option, action);
		const QSize text = fontMetrics().size(Qt::TextShowMnemonic, action->text());
		width += style()->sizeFromContents(QStyle::CT_MenuBarItem, &option, text, this).width() + spacing;
	}
	const QMargins margins = contentsMargins();
	return width + margins.left() + margins.right();
}

QSize StudioMenuBar::sizeHint() const
{
	QSize hint = QMenuBar::sizeHint();
	if (!isNativeMenuBar()) {
		hint.setWidth(naturalWidth());
	}
	return hint;
}

// ---------------------------------------------------------------------------
// CommandSearchButton
// ---------------------------------------------------------------------------

namespace {

constexpr int kSearchPaddingLeading = 10;
constexpr int kSearchPaddingTrailing = 5;
constexpr int kSearchKeyGap = 12;

} // namespace

CommandSearchButton::CommandSearchButton(QWidget* parent)
	: QToolButton(parent)
{
	setObjectName(QStringLiteral("commandSearchButton"));
	setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	setAttribute(Qt::WA_Hover, true);
}

void CommandSearchButton::setLabel(const QString& label, const QString& keys)
{
	m_label = label;
	m_keys = keys;
	setText(keys.isEmpty() ? label : QStringLiteral("%1  %2").arg(label, keys));
	updateGeometry();
	update();
}

QString CommandSearchButton::label() const
{
	return m_label;
}

QString CommandSearchButton::keys() const
{
	return m_keys;
}

QFont CommandSearchButton::keyFont() const
{
	QFont small = font();
	if (small.pointSizeF() > 0) {
		small.setPointSizeF(small.pointSizeF() * 0.88);
	}
	return small;
}

int CommandSearchButton::keyCapWidth() const
{
	return m_keys.isEmpty() ? 0 : keyCapsWidth(m_keys, keyFont());
}

QSize CommandSearchButton::sizeHint() const
{
	ensurePolished();
	const QFontMetrics metrics(font());
	int width = kSearchPaddingLeading + iconSize().width() + metrics.horizontalAdvance(m_label) + kSearchPaddingTrailing;
	if (!m_keys.isEmpty()) {
		width += kSearchKeyGap + keyCapWidth();
	}
	return {width, std::max(metrics.height(), iconSize().height()) + 12};
}

QSize CommandSearchButton::minimumSizeHint() const
{
	ensurePolished();
	const QFontMetrics metrics(font());
	// The glyph and the start of the label: enough to read as a search field.
	return {kSearchPaddingLeading + iconSize().width() + metrics.averageCharWidth() * 6 + kSearchPaddingTrailing,
		std::max(metrics.height(), iconSize().height()) + 12};
}

void CommandSearchButton::paintEvent(QPaintEvent*)
{
	QStylePainter painter(this);
	// The frame and fill come from the style sheet, hover and focus included;
	// the contents are laid out here.
	QStyleOptionToolButton option;
	initStyleOption(&option);
	option.text.clear();
	option.icon = QIcon();
	painter.drawComplexControl(QStyle::CC_ToolButton, option);

	const StudioThemeColors& colors = currentStudioTheme().colors;
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	const QRect area = mirrored ? rect().adjusted(kSearchPaddingTrailing, 0, -kSearchPaddingLeading, 0)
								: rect().adjusted(kSearchPaddingLeading, 0, -kSearchPaddingTrailing, 0);
	const QSize box = iconSize();
	const QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
	const QPixmap glyph = icon().pixmap(box, devicePixelRatioF(), mode);
	const int glyphTop = rect().center().y() - box.height() / 2 + 1;
	painter.drawPixmap(mirrored ? area.right() + 1 - box.width() : area.left(), glyphTop, glyph);

	const QFontMetrics metrics(font());
	int labelSpace = area.width() - box.width();
	// The key cap shows while the label keeps at least a few characters.
	const int keyWidth = keyCapWidth();
	const bool showKeys = keyWidth > 0 && labelSpace - keyWidth - kSearchKeyGap >= metrics.averageCharWidth() * 8;
	if (showKeys) {
		labelSpace -= keyWidth + kSearchKeyGap;
		paintKeyCaps(&painter, area, m_keys, keyFont(), layoutDirection(), isEnabled());
		painter.setFont(font());
	}
	const QColor labelColor = !isEnabled() ? colors.textFaint : (underMouse() ? colors.text : colors.textMuted);
	painter.setPen(labelColor);
	const QRect labelRect = mirrored ? QRect(area.right() + 1 - box.width() - labelSpace, area.top(), labelSpace, area.height())
									 : QRect(area.left() + box.width(), area.top(), labelSpace, area.height());
	painter.drawText(labelRect, Qt::AlignVCenter | (mirrored ? Qt::AlignRight : Qt::AlignLeft),
		metrics.elidedText(m_label, Qt::ElideRight, std::max(0, labelSpace)));
}

namespace {

// Places the centre widget for keepCentredInToolBar(). Positions are read
// after the bar's layout has run (the work is queued), measured from the
// bar's leading edge so a right-to-left bar centres the same way.
class CentreKeeper final : public QObject {
public:
	CentreKeeper(QToolBar* bar, QWidget* leadingSpace, QWidget* centre)
		: QObject(bar)
		, m_bar(bar)
		, m_leading(leadingSpace)
		, m_centre(centre)
	{
		m_leading->setFixedWidth(kGap);
		m_bar->installEventFilter(this);
		m_leading->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		switch (event->type()) {
		case QEvent::Resize:
		case QEvent::Move:
		case QEvent::Show:
		case QEvent::LayoutRequest:
		case QEvent::LayoutDirectionChange:
			schedule();
			break;
		default:
			break;
		}
		return QObject::eventFilter(watched, event);
	}

private:
	static constexpr int kGap = 8;

	void schedule()
	{
		if (m_scheduled) {
			return;
		}
		m_scheduled = true;
		QMetaObject::invokeMethod(this, [this]() {
			m_scheduled = false;
			place();
		}, Qt::QueuedConnection);
	}

	[[nodiscard]] int leadingEdgeOf(const QWidget* widget) const
	{
		return m_bar->layoutDirection() == Qt::RightToLeft ? m_bar->width() - widget->geometry().right() - 1 : widget->x();
	}

	void place()
	{
		if (!m_bar || !m_leading || !m_centre || !m_bar->isVisible() || !m_leading->isVisible()) {
			return;
		}
		// The widgets after the centre one keep their own widths; what lies
		// between the end of the leading items and the start of the trailing
		// ones is shared by the gap and the centre widget.
		int trailingWidth = 0;
		const int centreStart = leadingEdgeOf(m_centre);
		for (QWidget* child : m_bar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
			if (!child->isVisible() || child == m_centre || child == m_leading || leadingEdgeOf(child) <= centreStart
				|| (child->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag)) {
				continue;
			}
			trailingWidth += child->width() + 2;
		}
		const int start = leadingEdgeOf(m_leading);
		const int available = m_bar->width() - start - trailingWidth - 2 * kGap;
		const double scale = std::clamp(currentStudioTheme().textScalePercent, 50, 400) / 100.0;
		const int minimum = m_centre->minimumSizeHint().width();
		const int preferred = std::max(m_centre->sizeHint().width(), static_cast<int>(m_bar->width() * 0.30));
		const int widest = std::max(minimum, static_cast<int>(520 * scale));
		const int width = std::max(minimum, std::min({preferred, widest, available}));
		const int centred = (m_bar->width() - width) / 2;
		const int gap = std::clamp(centred - start, kGap, std::max(kGap, available + kGap - width));
		if (m_centre->width() != width || m_centre->minimumWidth() != width) {
			m_centre->setFixedWidth(width);
		}
		if (m_leading->width() != gap || m_leading->minimumWidth() != gap) {
			m_leading->setFixedWidth(gap);
		}
	}

	QPointer<QToolBar> m_bar;
	QPointer<QWidget> m_leading;
	QPointer<QWidget> m_centre;
	bool m_scheduled = false;
};

} // namespace

void keepCentredInToolBar(QToolBar* bar, QWidget* leadingSpace, QWidget* centre)
{
	if (!bar || !leadingSpace || !centre) {
		return;
	}
	new CentreKeeper(bar, leadingSpace, centre);
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

namespace {

// What a panel tab strip keeps per tab, as tab data, while it may show less:
// the tab's whole label, its glyph, and the tooltip the page gave it.
const QString kPanelTabText = QStringLiteral("text");
const QString kPanelTabIcon = QStringLiteral("icon");
const QString kPanelTabTip = QStringLiteral("tip");

void fitPanelTabs(QTabWidget* tabs)
{
	if (!tabs || tabs->property("fittingTabs").toBool()) {
		return;
	}
	tabs->setProperty("fittingTabs", true);
	QTabBar* bar = tabs->tabBar();
	const int count = bar->count();
	bool allIcons = count > 0;
	for (int index = 0; index < count; ++index) {
		QVariantMap data = bar->tabData(index).toMap();
		const QString shown = bar->tabText(index);
		// A tab added since, or relabelled from outside, shows its label now.
		if (data.isEmpty() || (!shown.isEmpty() && shown != data.value(kPanelTabText).toString())) {
			data.insert(kPanelTabText, shown);
			data.insert(kPanelTabTip, bar->tabToolTip(index));
		}
		// A glyph the strip took away comes back from here.
		if (!bar->tabIcon(index).isNull()) {
			data.insert(kPanelTabIcon, bar->tabIcon(index));
		}
		bar->setTabData(index, data);
		allIcons = allIcons && !data.value(kPanelTabIcon).value<QIcon>().isNull();
	}
	// Shows the labels `labelled` picks, with or without glyphs, and returns
	// the width that takes.
	const auto arrange = [bar, count](const std::function<bool(int)>& labelled, bool glyphs) {
		for (int index = 0; index < count; ++index) {
			const QVariantMap data = bar->tabData(index).toMap();
			const QString text = data.value(kPanelTabText).toString();
			const QString tip = data.value(kPanelTabTip).toString();
			const bool withLabel = labelled(index);
			if (bar->tabText(index) != (withLabel ? text : QString())) {
				bar->setTabText(index, withLabel ? text : QString());
			}
			bar->setTabIcon(index, glyphs ? data.value(kPanelTabIcon).value<QIcon>() : QIcon());
			bar->setTabToolTip(index, withLabel || !tip.isEmpty() ? tip : text);
			bar->setAccessibleTabName(index, text);
		}
		return bar->sizeHint().width();
	};
	int available = tabs->width();
	for (const Qt::Corner corner : {Qt::TopLeftCorner, Qt::TopRightCorner, Qt::BottomLeftCorner, Qt::BottomRightCorner}) {
		if (const QWidget* widget = tabs->cornerWidget(corner); widget && widget->isVisible()) {
			available -= widget->width();
		}
	}
	// Glyph-led tabs are drawn with tighter padding ("compactTabs" in the
	// style sheet): the style sheet already sets space aside beside each
	// glyph, and full padding on top would make a glyph-only tab nearly as
	// wide as a labelled one.
	const auto compact = [bar](bool on) {
		if (bar->property("compactTabs").toBool() != on) {
			bar->setProperty("compactTabs", on);
			bar->style()->unpolish(bar);
			bar->style()->polish(bar);
		}
	};
	// Whole labels first, then whole labels without glyphs, then the current
	// tab's label alone, then glyphs alone. Tabs without glyphs have nothing
	// to shrink to, so they keep eliding their labels.
	const auto all = [](int) {
		return true;
	};
	// The arrangement is kept on the strip as "tabLabels", for tests.
	QString labels = QStringLiteral("all");
	compact(false);
	if (arrange(all, true) > available) {
		labels = QStringLiteral("text");
		if (arrange(all, false) > available) {
			if (allIcons) {
				const int current = bar->currentIndex();
				labels = QStringLiteral("current");
				compact(true);
				if (arrange([current](int index) { return index == current; }, true) > available) {
					labels = QStringLiteral("icons");
					arrange([](int) { return false; }, true);
				}
			} else {
				labels = QStringLiteral("all");
				arrange(all, true);
			}
		}
	}
	tabs->setProperty("tabLabels", labels);
	tabs->setProperty("fittingTabs", false);
}

class PanelTabFitter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		switch (event->type()) {
		case QEvent::Resize:
		case QEvent::Show:
		case QEvent::FontChange:
		case QEvent::StyleChange:
			fitPanelTabs(static_cast<QTabWidget*>(watched));
			break;
		default:
			break;
		}
		return false;
	}
};

} // namespace

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
	auto* fitter = new PanelTabFitter(tabs);
	tabs->installEventFilter(fitter);
	// The current tab keeps its label longest, so a change of tab refits.
	QObject::connect(tabs, &QTabWidget::currentChanged, fitter, [tabs]() {
		fitPanelTabs(tabs);
	});
	return tabs;
}

void setPanelTabText(QTabWidget* tabs, int index, const QString& text)
{
	if (!tabs || index < 0 || index >= tabs->count()) {
		return;
	}
	QTabBar* bar = tabs->tabBar();
	QVariantMap data = bar->tabData(index).toMap();
	if (!data.contains(kPanelTabTip)) {
		data.insert(kPanelTabTip, bar->tabToolTip(index));
	}
	data.insert(kPanelTabText, text);
	bar->setTabData(index, data);
	bar->setTabText(index, text);
	fitPanelTabs(tabs);
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

namespace {

bool isStatusChip(const QObject* button)
{
	return button && button->objectName() == QLatin1String("statusChip");
}

// Shows a chip's glyph and text, or, folded for room, its icon and the
// state's glyph, with the text moved into the tooltip.
void applyStatusChipText(QAbstractButton* chip)
{
	auto* tool = qobject_cast<QToolButton*>(chip);
	const bool folded = tool && tool->property("statusFolded").toBool() && !tool->icon().isNull();
	const QString text = chip->property("chipText").toString();
	const QString toolTip = chip->property("chipToolTip").toString();
	if (folded) {
		tool->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		chip->setText(chip->property("chipGlyph").toString());
		chip->setToolTip(toolTip.isEmpty() ? text : QStringLiteral("%1\n%2").arg(text, toolTip));
		return;
	}
	if (tool) {
		tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
	}
	const QString display = chip->property("chipDisplay").toString();
	const int maximum = chip->maximumWidth() < QWIDGETSIZE_MAX ? chip->maximumWidth() - 24 : 0;
	chip->setText(maximum > 0 ? chip->fontMetrics().elidedText(display, Qt::ElideMiddle, maximum) : display);
	chip->setToolTip(toolTip);
}

// A folded button's label goes into its tooltip, ahead of what the tooltip
// said unfolded, so a count in the label is not lost with it.
void applyFoldedButtonTip(QToolButton* button)
{
	if (!button->property("unfoldedToolTip").isValid()) {
		button->setProperty("unfoldedToolTip", button->toolTip());
	}
	const QString tip = button->property("unfoldedToolTip").toString();
	const bool folded = button->property("statusFolded").toBool();
	button->setToolTip(folded ? (tip.isEmpty() ? button->text() : QStringLiteral("%1\n%2").arg(button->text(), tip)) : tip);
}

void setStatusButtonFolded(QToolButton* button, bool folded)
{
	if (button->property("statusFolded").toBool() == folded) {
		return;
	}
	if (!button->property("unfoldedStyle").isValid()) {
		button->setProperty("unfoldedStyle", static_cast<int>(button->toolButtonStyle()));
	}
	button->setProperty("statusFolded", folded);
	if (isStatusChip(button)) {
		applyStatusChipText(button);
	} else {
		button->setToolButtonStyle(folded ? Qt::ToolButtonIconOnly : static_cast<Qt::ToolButtonStyle>(button->property("unfoldedStyle").toInt()));
		applyFoldedButtonTip(button);
	}
}

// The width a status bar's widgets ask for, laid out the way QStatusBar lays
// them out: six pixels apart.
int statusWidgetsWidth(const QLayout* layout)
{
	int width = 0;
	for (int index = 0; index < layout->count(); ++index) {
		QLayoutItem* item = layout->itemAt(index);
		if (const QLayout* inner = item->layout()) {
			width += statusWidgetsWidth(inner);
		} else if (const QWidget* widget = item->widget(); widget && !widget->isHidden()) {
			width += widget->sizeHint().width() + 6;
		}
	}
	return width;
}

class StatusBarFitter final : public QObject {
public:
	StatusBarFitter(QStatusBar* bar, const QList<QToolButton*>& foldOrder)
		: QObject(bar)
		, m_bar(bar)
	{
		for (QToolButton* button : foldOrder) {
			if (button) {
				m_foldOrder << button;
			}
		}
		bar->installEventFilter(this);
	}

	// Coalesces the refits that several chip changes in a row would ask for.
	void scheduleFit()
	{
		if (!m_scheduled) {
			m_scheduled = true;
			QTimer::singleShot(0, this, [this]() {
				m_scheduled = false;
				fit();
			});
		}
	}

	void fit()
	{
		QStatusBar* bar = m_bar;
		if (!bar || !bar->layout() || m_fitting) {
			return;
		}
		const QScopedValueRollback<bool> guard(m_fitting, true);
		QList<QToolButton*> buttons;
		for (const QPointer<QToolButton>& button : std::as_const(m_foldOrder)) {
			if (button) {
				buttons << button;
			}
		}
		// Each button may shrink as far as folded, so the window never has to
		// stay wider than everything folded; then all start unfolded.
		for (QToolButton* button : buttons) {
			setStatusButtonFolded(button, true);
			button->setMinimumWidth(button->sizeHint().width());
			setStatusButtonFolded(button, false);
		}
		// About forty characters of message, the margins QStatusBar keeps
		// around it aside; fold in order until they fit.
		const int wanted = bar->fontMetrics().averageCharWidth() * 40;
		const int available = bar->width() - 18;
		int used = statusWidgetsWidth(bar->layout());
		for (QToolButton* button : buttons) {
			if (available - used >= wanted) {
				break;
			}
			const int before = button->sizeHint().width();
			setStatusButtonFolded(button, true);
			used -= before - button->sizeHint().width();
		}
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == m_bar) {
			switch (event->type()) {
			case QEvent::Resize:
				fit();
				break;
			case QEvent::Show:
			case QEvent::FontChange:
			case QEvent::StyleChange:
			case QEvent::LanguageChange:
				scheduleFit();
				break;
			default:
				break;
			}
		}
		return false;
	}

private:
	QPointer<QStatusBar> m_bar;
	QList<QPointer<QToolButton>> m_foldOrder;
	bool m_scheduled = false;
	bool m_fitting = false;
};

// A chip's new text can change how much room the bar's widgets need.
void refitStatusBarOf(QWidget* chip)
{
	for (QWidget* parent = chip->parentWidget(); parent; parent = parent->parentWidget()) {
		if (auto* bar = qobject_cast<QStatusBar*>(parent)) {
			for (QObject* child : bar->children()) {
				if (auto* fitter = dynamic_cast<StatusBarFitter*>(child)) {
					fitter->scheduleFit();
				}
			}
			return;
		}
	}
}

} // namespace

bool statusBarButtonFolded(const QToolButton* button)
{
	return button && button->property("statusFolded").toBool();
}

void setStatusBarButtonText(QToolButton* button, const QString& text)
{
	if (!button || button->text() == text) {
		return;
	}
	button->setText(text);
	if (button->property("unfoldedToolTip").isValid()) {
		applyFoldedButtonTip(button);
	}
	refitStatusBarOf(button);
}

void fitStatusBarToMessages(QStatusBar* bar, const QList<QToolButton*>& foldOrder)
{
	if (!bar) {
		return;
	}
	auto* fitter = new StatusBarFitter(bar, foldOrder);
	fitter->scheduleFit();
}

QToolButton* createStatusChip(const QString& accessibleName, const QString& iconName)
{
	auto* chip = new QToolButton;
	chip->setObjectName(QStringLiteral("statusChip"));
	chip->setAccessibleName(accessibleName);
	if (!iconName.isEmpty()) {
		chip->setIcon(studioIcon(iconName));
		setBaseIconSize(chip, QSize(14, 14));
	}
	chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
	chip->setProperty("operationState", QStringLiteral("idle"));
	chip->setCursor(Qt::PointingHandCursor);
	// Reachable with Tab; a click leaves focus where the user was working.
	chip->setFocusPolicy(Qt::TabFocus);
	// Long project or package names elide rather than widening the bar.
	chip->setMaximumWidth(260);
	return chip;
}

void setStatusChip(QAbstractButton* chip, const QString& operationStateId, const QString& text, const QString& toolTip)
{
	if (!chip) {
		return;
	}
	const QString glyph = studioStateGlyph(operationStateFromId(operationStateId));
	chip->setProperty("chipDisplay", glyph.isEmpty() ? text : QStringLiteral("%1 %2").arg(glyph, text));
	chip->setProperty("chipGlyph", glyph);
	chip->setProperty("chipText", text);
	chip->setProperty("chipToolTip", toolTip);
	applyStatusChipText(chip);
	chip->setAccessibleDescription(toolTip.isEmpty() ? text : toolTip);
	refitStatusBarOf(chip);
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

void fitListHeightToRows(QAbstractItemView* view, int minimumHeight, int maximumHeight)
{
	if (!view || !view->model()) {
		return;
	}
	const int rows = view->model()->rowCount(view->rootIndex());
	int content = 0;
	for (int row = 0; row < rows && content < maximumHeight; ++row) {
		if (const auto* list = qobject_cast<const QListView*>(view); list && list->isRowHidden(row)) {
			continue;
		}
		content += std::max(0, view->sizeHintForRow(row));
	}
	if (const auto* list = qobject_cast<const QListView*>(view)) {
		content += list->spacing() * std::max(0, rows - 1);
	}
	auto* frame = qobject_cast<QFrame*>(view);
	const int chrome = 2 * (frame ? frame->frameWidth() : 0) + 6;
	view->setMinimumHeight(minimumHeight);
	view->setMaximumHeight(std::clamp(content + chrome, minimumHeight, std::max(minimumHeight, maximumHeight)));
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

void clearOnEscape(QLineEdit* field)
{
	if (!field || field->property("vibestudioClearsOnEscape").toBool()) {
		return;
	}
	field->setProperty("vibestudioClearsOnEscape", true);
	field->installEventFilter(new EscapeClearsField(field));
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
