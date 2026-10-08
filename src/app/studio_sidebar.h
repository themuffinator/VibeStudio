#pragma once

// Tabbed side panels in the manner of Blender's Properties editor and the 3D
// view's sidebar: a column of glyph tabs running down one edge, each opening a
// page of collapsible sections.
//
// A StudioSidebar is a QTabWidget, so code that already drives panel tabs
// (indexOf, setCurrentWidget, currentChanged) keeps working. Its tab bar is a
// SidebarTabBar, which paints square glyph tiles, optional captions, gaps
// between groups of tabs and the current tab joined to its page. Clicking the
// current tab folds the sidebar down to its tab column; any tab opens it again.
//
// Pages are SidebarPages: a header row (title, a count, page actions) above a
// body of SidebarSections, each with a disclosure header that remembers
// whether it is open. Everything mirrors right to left, follows the text scale
// and the high-visibility themes, and is reachable from the keyboard.

#include <QAbstractButton>
#include <QPointer>
#include <QString>
#include <QTabBar>
#include <QTabWidget>
#include <QVector>
#include <QWidget>

class QBoxLayout;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace vibestudio {

class SidebarPage;

// The vertical tab column. Each tab is a tile holding its page's glyph, and,
// when captions are on, the page title underneath. The tab text is always the
// page title, so assistive technology and tooltips name icon-only tabs.
class SidebarTabBar final : public QTabBar {
	Q_OBJECT

public:
	explicit SidebarTabBar(QWidget* parent = nullptr);

	// A tab that starts a group is set apart from the one above it by a gap
	// with a short rule, the way Blender groups its Properties tabs.
	void setGroupStart(int index, bool starts);
	[[nodiscard]] bool startsGroup(int index) const;
	void setShowLabels(bool show);
	[[nodiscard]] bool showsLabels() const;
	// While folded no tab is joined to a page: the current one is only marked.
	void setFolded(bool folded);
	[[nodiscard]] bool isFolded() const;
	// The width of the column, the same for every tab.
	[[nodiscard]] int columnWidth() const;
	// The tab under `position`, ignoring the gaps between groups.
	[[nodiscard]] int tabAtPosition(const QPoint& position) const;

Q_SIGNALS:
	// A right click, or the context-menu key, on a tab.
	void tabMenuRequested(int index, const QPoint& globalPosition);

protected:
	[[nodiscard]] QSize tabSizeHint(int index) const override;
	[[nodiscard]] QSize minimumTabSizeHint(int index) const override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	[[nodiscard]] int groupGap() const;
	[[nodiscard]] int glyphSize() const;
	[[nodiscard]] QRect tileRect(int index) const;
	[[nodiscard]] bool pageOnRight() const;

	bool m_showLabels = false;
	bool m_folded = false;
	int m_hover = -1;
};

class StudioSidebar final : public QTabWidget {
	Q_OBJECT

public:
	// Which edge of the sidebar its tab column runs along, in reading order:
	// Leading is the left edge left to right and the right edge right to left.
	enum class TabEdge {
		Leading,
		Trailing,
	};

	explicit StudioSidebar(const QString& accessibleName, TabEdge edge, QWidget* parent = nullptr);

	[[nodiscard]] SidebarTabBar* sidebarTabBar() const;
	[[nodiscard]] TabEdge tabEdge() const;
	void setTabEdge(TabEdge edge);

	// Adds a page as a tab; its glyph, title and description name the tab.
	int addPage(SidebarPage* page, bool startsGroup = false);
	// Takes a page out without deleting it, so it can move to another sidebar.
	SidebarPage* takePage(const QString& pageId);
	[[nodiscard]] SidebarPage* page(const QString& pageId) const;
	[[nodiscard]] SidebarPage* pageAt(int index) const;
	[[nodiscard]] QStringList pageIds() const;
	[[nodiscard]] int indexOfPage(const QString& pageId) const;
	[[nodiscard]] QString currentPageId() const;
	// Makes the page current and opens the sidebar if it was folded. False when
	// this sidebar has no such page.
	bool showPage(const QString& pageId);

	// Folds the sidebar to its tab column, or opens it again at the width it
	// had. Inside a QSplitter the neighbours take or give back the width.
	void setFolded(bool folded);
	[[nodiscard]] bool isFolded() const;
	void setShowLabels(bool show);
	[[nodiscard]] bool showsLabels() const;
	// The width the sidebar occupies while folded.
	[[nodiscard]] int foldedWidth() const;
	// The width it reopens at.
	[[nodiscard]] int expandedWidth() const;
	void setExpandedWidth(int width);

	[[nodiscard]] QSize minimumSizeHint() const override;
	[[nodiscard]] QSize sizeHint() const override;

Q_SIGNALS:
	void foldedChanged(bool folded);
	void currentPageChanged(const QString& pageId);
	void tabMenuRequested(const QString& pageId, const QPoint& globalPosition);

protected:
	void changeEvent(QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void tabInserted(int index) override;
	void tabRemoved(int index) override;

private:
	void applyTabPosition();
	void refreshTab(int index);
	void refreshFoldedGeometry();
	[[nodiscard]] QStackedWidget* stack() const;

	SidebarTabBar* m_bar = nullptr;
	TabEdge m_edge = TabEdge::Leading;
	bool m_folded = false;
	bool m_applyingFold = false;
	int m_expandedWidth = 0;
};

// The disclosure row above a section's body: a chevron, the title and an
// optional count, painted to match the studio theme.
class SidebarSectionHeader final : public QAbstractButton {
	Q_OBJECT

public:
	explicit SidebarSectionHeader(const QString& title, QWidget* parent = nullptr);

	void setBadge(const QString& badge);
	[[nodiscard]] QString badge() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	QString m_badge;
	bool m_hovered = false;
};

class SidebarSection final : public QWidget {
	Q_OBJECT

public:
	SidebarSection(const QString& sectionId, const QString& title, QWidget* body, QWidget* parent = nullptr);

	[[nodiscard]] QString sectionId() const;
	[[nodiscard]] QString title() const;
	void setTitle(const QString& title);
	void setBadge(const QString& badge);
	[[nodiscard]] QWidget* body() const;
	[[nodiscard]] SidebarSectionHeader* header() const;
	// A small icon-only button at the end of the header row.
	QToolButton* addHeaderButton(const QString& iconName, const QString& text, const QString& toolTip);

	void setExpanded(bool expanded);
	[[nodiscard]] bool isExpanded() const;
	// How much of the page's spare height an open section takes; a folded
	// section takes none.
	void setStretch(int stretch);
	[[nodiscard]] int stretch() const;

Q_SIGNALS:
	void expandedChanged(bool expanded);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QString m_id;
	SidebarSectionHeader* m_header = nullptr;
	QHBoxLayout* m_headerLayout = nullptr;
	QWidget* m_bodyFrame = nullptr;
	QWidget* m_body = nullptr;
	int m_stretch = 0;
};

class SidebarPage final : public QWidget {
	Q_OBJECT

public:
	SidebarPage(const QString& pageId, const QString& iconName, const QString& title, const QString& description, QWidget* parent = nullptr);

	[[nodiscard]] QString pageId() const;
	[[nodiscard]] QString iconName() const;
	[[nodiscard]] QString title() const;
	void setTitle(const QString& title);
	[[nodiscard]] QString description() const;
	void setDescription(const QString& description);
	// A short count beside the title, such as "24" or "3 of 40".
	void setBadge(const QString& badge);
	[[nodiscard]] QString badge() const;
	// A key that shows the page, named in its tab's tooltip.
	void setShortcutText(const QString& shortcut);
	[[nodiscard]] QString shortcutText() const;

	QToolButton* addHeaderButton(const QString& iconName, const QString& text, const QString& toolTip);
	void addHeaderWidget(QWidget* widget);

	// Pages scroll as a whole unless a section stretches to fill them, as a
	// list does. Choose before adding content.
	void setScrollable(bool scrollable);
	[[nodiscard]] bool isScrollable() const;
	void addWidget(QWidget* widget, int stretch = 0);
	SidebarSection* addSection(const QString& sectionId, const QString& title, QWidget* body, int stretch = 0);
	// Counts a section that sits inside a widget added with addWidget as one
	// of the page's own: section() finds it and its toggles are reported.
	void adoptSection(SidebarSection* section);
	[[nodiscard]] QVector<SidebarSection*> sections() const;
	[[nodiscard]] SidebarSection* section(const QString& sectionId) const;

Q_SIGNALS:
	// The title, description, badge or shortcut changed: the tab follows.
	void presentationChanged();
	void sectionExpandedChanged(const QString& sectionId, bool expanded);

private:
	void refreshHeader();
	void refreshStretch();
	[[nodiscard]] QVBoxLayout* contentLayout();

	QString m_id;
	QString m_iconName;
	QString m_title;
	QString m_description;
	QString m_badge;
	QString m_shortcut;
	bool m_scrollable = true;
	QLabel* m_titleLabel = nullptr;
	QLabel* m_badgeLabel = nullptr;
	QHBoxLayout* m_headerLayout = nullptr;
	QVBoxLayout* m_rootLayout = nullptr;
	QScrollArea* m_scroll = nullptr;
	QWidget* m_content = nullptr;
	QVBoxLayout* m_contentLayout = nullptr;
	QVector<SidebarSection*> m_sections;
};

} // namespace vibestudio
