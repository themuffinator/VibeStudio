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
#include <QTabWidget>
#include <QString>
#include <QVector>
#include <QWidget>

class QAbstractItemView;
class QBoxLayout;
class QButtonGroup;
class QDockWidget;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QSplitter;
class QTabWidget;
class QToolBar;
class QToolButton;
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

// Vertical navigation rail. Entries are checkable tool buttons, so each one is
// announced by assistive technology as a button with a checked state, and the
// arrow keys, Home, and End move between them like a tab list.
class ModeRail final : public QWidget {
	Q_OBJECT

public:
	explicit ModeRail(QWidget* parent = nullptr);

	void setEntries(const QVector<ModeRailEntry>& entries);
	[[nodiscard]] int count() const;
	[[nodiscard]] int currentId() const;
	// Selects an entry without emitting currentIdChanged.
	void setCurrentId(int id);
	[[nodiscard]] QString label(int id) const;

	void setCompact(bool compact);
	[[nodiscard]] bool isCompact() const;

Q_SIGNALS:
	void currentIdChanged(int id);
	void compactChanged(bool compact);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void rebuild();
	void refreshCompactPresentation();
	void focusRelative(int step);

	QVector<ModeRailEntry> m_entries;
	QVector<QToolButton*> m_buttons;
	QButtonGroup* m_group = nullptr;
	QVBoxLayout* m_topLayout = nullptr;
	QVBoxLayout* m_bottomLayout = nullptr;
	QToolButton* m_toggle = nullptr;
	bool m_compact = false;
};

// Page title bar: icon, title, a one-line context summary, an optional status
// widget (usually a state chip), and the page's action buttons on the right.
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

protected:
	void changeEvent(QEvent* event) override;

private:
	void refreshIcon();

	QLabel* m_icon = nullptr;
	QLabel* m_title = nullptr;
	ElidedLabel* m_subtitle = nullptr;
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

protected:
	void changeEvent(QEvent* event) override;

private:
	void refreshIcon();

	QLabel* m_icon = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_body = nullptr;
	QHBoxLayout* m_actions = nullptr;
	QString m_iconName;
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

	QDockWidget* m_dock = nullptr;
	ElidedLabel* m_title = nullptr;
	QToolButton* m_float = nullptr;
	QToolButton* m_close = nullptr;
};

// Factories that apply the studio's object names, sizes, and accessibility
// metadata consistently.
QToolBar* createPageToolBar(const QString& accessibleName, QWidget* parent = nullptr);
QToolButton* createToolButton(const QString& iconName, const QString& text, const QString& toolTip, bool showText = true);
QPushButton* createButton(const QString& text, const QString& iconName = QString(), const QString& variant = QString());
QSplitter* createSplitter(Qt::Orientation orientation, const QString& objectName, const QString& accessibleName);
// Tab group for a panel. Side and bottom panel groups put their tabs along
// the bottom edge, the way idStudio's docked panels do; page-level sections
// keep them on top.
QTabWidget* createPanelTabs(const QString& accessibleName, QTabWidget::TabPosition position = QTabWidget::North);
QScrollArea* createScrollSurface(QWidget* content, const QString& accessibleName);
QLabel* createStatusChip(const QString& accessibleName);
// Sets a chip's text and state; the text always names the state so the chip
// never relies on colour alone.
void setStatusChip(QLabel* chip, const QString& operationStateId, const QString& text, const QString& toolTip = QString());
QFrame* createDivider(Qt::Orientation orientation = Qt::Horizontal);
// Fits a list's rows to its width and elides each text line instead of
// growing a horizontal scroll bar. Paths elide in the middle by default so
// both the root and the file name stay visible.
void useElidingRows(QAbstractItemView* view, Qt::TextElideMode mode = Qt::ElideMiddle);
// Records the icon size a control was designed at (at 100% text scale).
void setBaseIconSize(QWidget* control, const QSize& size);
// Scales every recorded icon size, and the icons of item views and tool bars,
// to the current text scale so glyphs grow with the text they sit beside.
void applyStudioIconScale(QWidget* root);
// A size scaled by the current text scale.
[[nodiscard]] QSize scaledIconSize(const QSize& base);
// Wraps a widget with a small caption above it, for form-like tool strips.
QWidget* createLabeledField(const QString& caption, QWidget* field);

} // namespace vibestudio
