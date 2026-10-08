#pragma once

// Shared building blocks for the studio's work surfaces.
//
// Every page is assembled from the same parts so the studio reads as one
// product: a PageHeader (icon, title, context line, status, actions), an
// optional page tool bar, a body built from splitters and panel tabs, and an
// EmptyStateView that replaces the body while there is nothing to show. The
// ModeRail switches between pages. Visual styling for all of these lives in
// studio_theme.cpp and is keyed on the object names set here.

#include <QFrame>
#include <QLabel>
#include <QMenuBar>
#include <QTabWidget>
#include <QString>
#include <QToolButton>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QAbstractItemView;
class QBoxLayout;
class QButtonGroup;
class QDockWidget;
class QEnterEvent;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPainter;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStatusBar;
class QTabWidget;
class QTimer;
class QToolBar;
class QToolButton;
class QVariantAnimation;
class QVBoxLayout;

namespace vibestudio {

// Single-line label that elides instead of growing. Readouts and context
// lines change constantly and can hold long paths; a plain QLabel's minimum
// width is its whole text, which would widen the window and crush the side
// panels. The full text stays available as the tooltip and accessible text.
class ElidedLabel final : public QLabel {
public:
	explicit ElidedLabel(const QString& text = QString(), QWidget* parent = nullptr);

	// Hides QLabel::setText on purpose: callers holding an ElidedLabel* get
	// elision; the displayed text is always derived from the full text.
	void setText(const QString& text);
	[[nodiscard]] QString fullText() const;
	void setElideMode(Qt::TextElideMode mode);

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void refreshElision();

	QString m_fullText;
	Qt::TextElideMode m_mode = Qt::ElideRight;
};

struct ModeRailEntry {
	int id = 0;
	QString label;
	QString hint;
	QString iconName;
	bool startsGroup = false;   // Draw a divider above this entry.
	bool pinnedToBottom = false; // Place below the flexible space (Settings).
};

// How the navigation rail spends its width.
enum class RailBehaviour {
	// The default: the rail rests as a column of icons and opens over the
	// page, labels and all, while the pointer rests on it or keyboard focus is
	// in it. The page never moves underneath.
	Automatic,
	// Always shows its labels and takes that width from the page, except in a
	// window too narrow to spare it, where it acts as Automatic.
	Expanded,
	// Icons only; each icon's tooltip names it.
	Compact,
};

[[nodiscard]] QString railBehaviourId(RailBehaviour behaviour);
[[nodiscard]] RailBehaviour railBehaviourFromId(const QString& id);
[[nodiscard]] QString localizedRailBehaviourName(RailBehaviour behaviour);

// Vertical navigation rail. Entries are checkable tool buttons, so each one is
// announced by assistive technology as a button with a checked state, and the
// arrow keys, Home, and End move between them like a tab list.
//
// The rail is placed by a RailHost, which reserves its resting width beside
// the page and lets it grow over the page while it is open.
class ModeRail final : public QWidget {
	Q_OBJECT

public:
	explicit ModeRail(QWidget* parent = nullptr);

	void setEntries(const QVector<ModeRailEntry>& entries);
	[[nodiscard]] int count() const;
	[[nodiscard]] int currentId() const;
	// Selects an entry without emitting currentIdChanged.
	void setCurrentId(int id);
	// Replaces an entry's hint (its tooltip's second line and accessible
	// description), such as when the key that selects it changes.
	void setHint(int id, const QString& hint);
	[[nodiscard]] QString label(int id) const;

	void setBehaviour(RailBehaviour behaviour);
	[[nodiscard]] RailBehaviour behaviour() const;
	// Expanded acts as Automatic in a window this narrow or narrower; the
	// host reports its width as it changes.
	void setAvailableWidth(int width);
	[[nodiscard]] RailBehaviour effectiveBehaviour() const;
	// Opens the labels over the page, or closes them, as hover and focus do.
	// Only an Automatic rail opens; the others ignore this.
	void setOpen(bool open);
	[[nodiscard]] bool isOpen() const;
	[[nodiscard]] bool showsLabels() const;
	// The width the page gives up for the rail, and the width it has now:
	// wider while open over the page.
	[[nodiscard]] int restingWidth() const;
	[[nodiscard]] int currentWidth() const;
	[[nodiscard]] int compactWidth() const;
	[[nodiscard]] int expandedWidth() const;
	// Opening and closing slide unless motion is reduced.
	void setReducedMotion(bool reduced);

	// How long the pointer rests before the rail opens, and how long it may
	// stray before it closes.
	static constexpr int kOpenDelayMsecs = 220;
	static constexpr int kCloseDelayMsecs = 320;

Q_SIGNALS:
	void currentIdChanged(int id);
	// The user pinned the labels open, or let them collapse, with the
	// rail's own button.
	void behaviourChanged(vibestudio::RailBehaviour behaviour);
	// Escape in the rail: the page should take the focus back.
	void dismissed();
	// The resting or current width moved; the host places things again.
	void geometryNeeded();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void changeEvent(QEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	// Draws the current page's marker: a short rounded bar on the rail's outer
	// edge, beside the highlighted entry.
	void paintEvent(QPaintEvent* event) override;

private:
	void rebuild();
	void refreshPresentation(bool animate = false);
	void refreshToggle();
	void refreshMargins();
	void showLabels(bool shown);
	void animateTo(int width);
	void closeUnlessHeld();
	[[nodiscard]] bool holdsFocus() const;
	void focusRelative(int step);

	QVector<ModeRailEntry> m_entries;
	QVector<QToolButton*> m_buttons;
	QButtonGroup* m_group = nullptr;
	QVBoxLayout* m_topLayout = nullptr;
	QVBoxLayout* m_bottomLayout = nullptr;
	QHBoxLayout* m_toggleRow = nullptr;
	QToolButton* m_toggle = nullptr;
	RailBehaviour m_behaviour = RailBehaviour::Automatic;
	// What the toggle returns to when the labels are unpinned: Automatic,
	// or Compact when the user chose icons only.
	RailBehaviour m_collapsedBehaviour = RailBehaviour::Automatic;
	int m_availableWidth = 0;
	bool m_open = false;
	bool m_hovered = false;
	bool m_labelsShown = false;
	// After a click picks a page the rail closes, and stays closed until the
	// pointer has left it; otherwise it would open again under the pointer.
	bool m_waitForLeave = false;
	bool m_reducedMotion = false;
	int m_currentWidth = 0;
	QTimer* m_openTimer = nullptr;
	QTimer* m_closeTimer = nullptr;
	QVariantAnimation* m_animation = nullptr;
};

// Holds the ModeRail and the page area side by side. The page starts where
// the rail's resting width ends; while the rail is open it lies over the
// page's edge with a soft shade beside it, so opening it never reflows the
// page underneath.
class RailHost final : public QWidget {
public:
	RailHost(ModeRail* rail, QWidget* content, QWidget* parent = nullptr);

protected:
	bool event(QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void placeRail();

	ModeRail* m_rail = nullptr;
	QWidget* m_content = nullptr;
	QWidget* m_footprint = nullptr;
	QWidget* m_shade = nullptr;
};

// A short fade over a work surface that has just come into view, so moving
// between pages reads as a transition rather than a jump cut. It lies over
// the host (the page stack), follows its size, takes no input and no focus,
// and does nothing under reduced motion or without a screen to show it on.
class PageTransition final : public QWidget {
public:
	explicit PageTransition(QWidget* host);

	void setReducedMotion(bool reduced);
	// Starts the fade over whatever the host shows now.
	void play();
	[[nodiscard]] bool isPlaying() const;

	static constexpr int kDurationMsecs = 170;

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QWidget* m_host = nullptr;
	QVariantAnimation* m_animation = nullptr;
	qreal m_strength = 0.0;
	bool m_reducedMotion = false;
};

// Page title bar, one slim row: icon, title, then a context summary of what
// the page shows (eliding in the middle), an optional status widget (usually
// a state chip), and the page's action buttons on the right.
// When the row runs short of room, the actions fold to their glyphs, the
// primary one last, with each label kept as the button's tooltip and name,
// so a long row of actions never holds the window wider than the screen.
// While the page shows its empty state, the header's primary action steps
// back to the secondary look, so the empty state's own call to action is the
// one emphasised button on screen.
class PageHeader final : public QWidget {
public:
	PageHeader(const QString& iconName, const QString& title, QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setSubtitle(const QString& subtitle);
	[[nodiscard]] QLabel* titleLabel() const;
	[[nodiscard]] ElidedLabel* subtitleLabel() const;
	void addStatusWidget(QWidget* widget);
	void addActionWidget(QWidget* widget);
	void addActionSeparator();
	// While muted, primary actions take the secondary look (and fold with the
	// secondary ones); unmuted, they get their emphasis back.
	void setPrimaryActionMuted(bool muted);
	[[nodiscard]] bool primaryActionMuted() const;

	// The width with every foldable action folded.
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void changeEvent(QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void refreshIcon();
	void fitActions();
	[[nodiscard]] QVector<QPushButton*> foldableActions() const;

	QLabel* m_icon = nullptr;
	QLabel* m_title = nullptr;
	QFrame* m_divider = nullptr;
	ElidedLabel* m_subtitle = nullptr;
	QWidget* m_titleStretch = nullptr;
	bool m_fitting = false;
	bool m_primaryMuted = false;
	QHBoxLayout* m_statusLayout = nullptr;
	QHBoxLayout* m_actionLayout = nullptr;
	QString m_iconName;
};

// Centred placeholder shown instead of a page body while it has no content,
// with the one or two actions that would give it some.
class EmptyStateView final : public QWidget {
public:
	EmptyStateView(const QString& iconName, const QString& title, const QString& body, QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setBody(const QString& body);
	QPushButton* addAction(const QString& text, const QString& iconName, bool primary);
	// Content under the actions, such as a list of recent files; replaces any
	// earlier one. The view takes ownership.
	void setDetail(QWidget* widget);

protected:
	void changeEvent(QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void refreshIcon();

	void fitActions();
	QLabel* m_icon = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_body = nullptr;
	QHBoxLayout* m_actions = nullptr;
	QVBoxLayout* m_column = nullptr;
	QWidget* m_detail = nullptr;
	QString m_iconName;
};

// Items laid out in as many equal columns as fit, up to a maximum, so a row
// of tiles wraps onto more rows instead of holding the page wider than the
// window. It asks for one column's width at least.
class ReflowGrid final : public QWidget {
public:
	explicit ReflowGrid(int maximumColumns, QWidget* parent = nullptr);

	void addItem(QWidget* widget);
	void setSpacing(int spacing);
	// Relative widths of the columns while all of them are in use; fewer
	// columns share the width equally.
	void setColumnStretches(const QVector<int>& stretches);
	// The width a column needs, for items whose own size hints say little
	// (cards around lists); by default each item's preferred width decides.
	void setColumnWidth(int width);
	[[nodiscard]] int columns() const;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	[[nodiscard]] int columnsFor(int width) const;
	void reflow(int columns);

	QGridLayout* m_grid = nullptr;
	QVector<QWidget*> m_items;
	QVector<int> m_stretches;
	int m_columnWidth = 0;
	int m_maximumColumns = 1;
	int m_columns = 0;
};

// Rounded panel with a title row (title, meta text, header widgets) and a body.
class CardFrame final : public QFrame {
public:
	explicit CardFrame(const QString& title = QString(), QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setMeta(const QString& meta);
	[[nodiscard]] ElidedLabel* metaLabel() const;
	void addHeaderWidget(QWidget* widget);
	[[nodiscard]] QVBoxLayout* bodyLayout() const;

private:
	QLabel* m_title = nullptr;
	ElidedLabel* m_meta = nullptr;
	QHBoxLayout* m_header = nullptr;
	QVBoxLayout* m_body = nullptr;
};

// Title bar for a docked panel: the panel's name with float and close buttons
// drawn from the studio glyphs, in place of the platform's small title bar
// buttons. Presses that miss the buttons fall through to QDockWidget, so
// dragging the bar still moves the panel and double-clicking still floats it.
class DockTitleBar final : public QWidget {
	Q_OBJECT

public:
	explicit DockTitleBar(QDockWidget* dock);

protected:
	void changeEvent(QEvent* event) override;

private:
	void refresh();
	void refreshMargins();

	QDockWidget* m_dock = nullptr;
	ElidedLabel* m_title = nullptr;
	QToolButton* m_float = nullptr;
	QToolButton* m_close = nullptr;
};

// Slim bar across the top of the work area for something the user should know
// and may act on, such as a previous session that ended in a crash: a state
// glyph, a title that names the state in words, the message, its actions, and
// a close button. Its leading edge carries the state's colour on the side the
// layout direction puts first. It stays until acted on or dismissed (Esc while
// focus is in it), never takes focus itself, and is announced as an alert when
// it shows.
class NoticeBar final : public QFrame {
	Q_OBJECT

public:
	explicit NoticeBar(QWidget* parent = nullptr);

	// Replaces any current notice, and its actions, and shows the bar.
	// `operationStateId` ("failed", "warning", "completed", "running", ...)
	// picks the glyph and the tint.
	void showNotice(const QString& operationStateId, const QString& title, const QString& text);
	// An action does not dismiss the bar by itself; its handler decides.
	QPushButton* addAction(const QString& text, const QString& iconName = QString(), bool primary = false);
	// Hides the bar, drops its actions, and emits dismissed().
	void dismiss();
	[[nodiscard]] QString title() const;
	[[nodiscard]] QString text() const;
	[[nodiscard]] QString stateId() const;

Q_SIGNALS:
	void dismissed();

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void changeEvent(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void clearActions();
	void refreshIcon();
	void refreshMargins();

	QLabel* m_icon = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_text = nullptr;
	QHBoxLayout* m_actions = nullptr;
	QToolButton* m_close = nullptr;
	QString m_stateId;
};

// A large button leading to a work surface: its glyph in a tinted well, its
// name, and a line saying what it holds right now ("foundry.map · 24
// entities"), so a start page's tiles carry real context instead of repeating
// the navigation rail. The line elides; the whole of it is the tooltip and the
// spoken description.
class NavigationTile final : public QAbstractButton {
public:
	NavigationTile(const QString& iconName, const QString& title, QWidget* parent = nullptr);

	void setDetail(const QString& detail);
	[[nodiscard]] QString detail() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	bool event(QEvent* event) override;

private:
	[[nodiscard]] QFont titleFont() const;
	[[nodiscard]] int wellSide() const;

	QString m_iconName;
	QString m_detail;
	QString m_baseToolTip;
};

// The menus as they sit in the studio bar. QMenuBar measures its preferred
// width from where its items currently stand, which in a right-to-left
// layout is the right edge of whatever width it was first given, so inside a
// tool bar it would keep that first width and push menus into its overflow.
// This one measures its items themselves, the same way in either direction.
class StudioMenuBar final : public QMenuBar {
public:
	using QMenuBar::QMenuBar;

	[[nodiscard]] QSize sizeHint() const override;
	// The width the menus need when every one of them shows.
	[[nodiscard]] int naturalWidth() const;
};

// The studio bar's command search: a field-like button that opens the command
// palette. It draws a magnifier and its label on the leading side and the
// palette's key as a key cap on the trailing side. Short of room, the label
// elides first and the key cap goes after. text() stays "label  key", so a
// screen reader, and anything reading the button, gets both.
class CommandSearchButton final : public QToolButton {
public:
	explicit CommandSearchButton(QWidget* parent = nullptr);

	void setLabel(const QString& label, const QString& keys);
	[[nodiscard]] QString label() const;
	[[nodiscard]] QString keys() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	[[nodiscard]] QFont keyFont() const;
	[[nodiscard]] int keyCapWidth() const;

	QString m_label;
	QString m_keys;
};

// A shortcut drawn as key caps, one per key ("Ctrl+Shift+P" as Ctrl, Shift,
// P; chords a little apart), at the trailing end of `area` and centred in its
// height. `onSelection` draws them for a highlighted row. Returns the width
// they take; keyCapsWidth() measures without drawing.
int paintKeyCaps(QPainter* painter, const QRect& area, const QString& shortcut, const QFont& font, Qt::LayoutDirection direction,
	bool enabled = true, bool onSelection = false);
[[nodiscard]] int keyCapsWidth(const QString& shortcut, const QFont& font);

// Gives a frameless floating dialog (the command palette, Go to File) rounded
// corners and a soft shadow where the window system composites translucent
// windows, and returns the margin the dialog must keep around its content for
// the shadow (0 where it keeps square corners). The dialog paints itself with
// paintFloatingPanel() and sets its layout margins from the result.
int prepareFloatingPanel(QWidget* dialog);
void paintFloatingPanel(QWidget* dialog, int shadowMargin);

// Keeps `centre` in the middle of the window's width within `bar`, as far as
// the items before it allow: `leadingSpace`, a plain widget placed just before
// it, takes up the difference. The centre widget takes about three tenths of
// the bar (at least its own size hint, at most 520 pixels at 100% text), and
// gives way down to its minimum size hint when the items around it need the
// room, so it is wide on a wide window and narrow on a narrow one.
void keepCentredInToolBar(QToolBar* bar, QWidget* leadingSpace, QWidget* centre);

// Factories that apply the studio's object names, sizes, and accessibility
// metadata consistently.
QToolBar* createPageToolBar(const QString& accessibleName, QWidget* parent = nullptr);
QToolButton* createToolButton(const QString& iconName, const QString& text, const QString& toolTip, bool showText = true);
QPushButton* createButton(const QString& text, const QString& iconName = QString(), const QString& variant = QString());
QSplitter* createSplitter(Qt::Orientation orientation, const QString& objectName, const QString& accessibleName);
// Tab group for a panel. Side and bottom panel groups put their tabs along
// the bottom edge, the way idStudio's docked panels do; page-level sections
// keep them on top.
// Panel tabs keep their labels readable in whatever width they get: every
// label when they fit, else only the current tab's with the rest as icons,
// else icons alone. A label not shown stays the tab's tooltip and spoken name.
QTabWidget* createPanelTabs(const QString& accessibleName, QTabWidget::TabPosition position = QTabWidget::North);
// Relabels a panel tab, such as a count in its name, keeping the fitting
// above in step with the new label.
void setPanelTabText(QTabWidget* tabs, int index, const QString& text);
QScrollArea* createScrollSurface(QWidget* content, const QString& accessibleName);
// Status chips are buttons: each one opens the surface that can act on what
// it reports, and is reachable from the keyboard like any other button.
// `iconName` names what the chip reports on; a folded chip shows it.
QToolButton* createStatusChip(const QString& accessibleName, const QString& iconName = QString());
// Sets a chip's text and state; the text always names the state so the chip
// never relies on colour alone.
void setStatusChip(QAbstractButton* chip, const QString& operationStateId, const QString& text, const QString& toolTip = QString());
// Keeps room for status messages. When the bar's permanent widgets would leave
// less than about forty characters for a message, the buttons in `foldOrder`
// fold one by one: a status chip to its icon and state mark, any other tool
// button to its icon. A folded label moves into the tooltip, the spoken name
// stays, and the bar can always shrink as far as everything folded.
void fitStatusBarToMessages(QStatusBar* bar, const QList<QToolButton*>& foldOrder);
// Whether the status bar has folded this button for room.
bool statusBarButtonFolded(const QToolButton* button);
// Relabels a status bar button, such as the Activity toggle's running count,
// keeping the label in its tooltip while it is folded, and refitting the bar.
void setStatusBarButtonText(QToolButton* button, const QString& text);
QFrame* createDivider(Qt::Orientation orientation = Qt::Horizontal);
// Fits a list's rows to its width and elides each text line instead of
// growing a horizontal scroll bar. Paths elide in the middle by default so
// both the root and the file name stay visible.
void useElidingRows(QAbstractItemView* view, Qt::TextElideMode mode = Qt::ElideMiddle);
// Lets a list ask for the height its rows need, between `minimumHeight` and
// `maximumHeight`, so a list with one or two rows does not stretch the card
// around it. Call it again after the rows change.
void fitListHeightToRows(QAbstractItemView* view, int minimumHeight, int maximumHeight);
// Records the icon size a control was designed at (at 100% text scale).
void setBaseIconSize(QWidget* control, const QSize& size);
// Scales every recorded icon size, and the icons of item views and tool bars,
// to the current text scale so glyphs grow with the text they sit beside.
void applyStudioIconScale(QWidget* root);
// A size scaled by the current text scale.
[[nodiscard]] QSize scaledIconSize(const QSize& base);
// Wraps a widget with a small caption above it, for form-like tool strips.
QWidget* createLabeledField(const QString& caption, QWidget* field);
// Escape empties a search or filter field first; once the field is empty the
// key reaches the surface as usual, so it can still cancel or close there.
void clearOnEscape(QLineEdit* field);

} // namespace vibestudio
