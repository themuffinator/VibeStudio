// Checks the Blender-style sidebar parts: the glyph tab column, folding inside
// a splitter, right-to-left placement, collapsible sections with their
// accessible state and keys, stretching pages, captions, and moving a page to
// the other sidebar.

#include "app/studio_sidebar.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

void settle()
{
	for (int pass = 0; pass < 4; ++pass) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	}
}

void click(QWidget* widget, const QPoint& point, Qt::MouseButton button = Qt::LeftButton)
{
	const QPointF local(point);
	const QPointF global(widget->mapToGlobal(point));
	QMouseEvent press(QEvent::MouseButtonPress, local, global, button, button, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QMouseEvent release(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(widget, &release);
	settle();
}

void key(QWidget* widget, int code)
{
	QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
	QApplication::sendEvent(widget, &release);
	settle();
}

QPoint tabCentre(const SidebarTabBar* bar, int index)
{
	QRect rect = bar->tabRect(index);
	return QPoint(rect.center().x(), rect.bottom() - 6);
}

SidebarPage* listPage(const QString& id, const QString& title)
{
	auto* page = new SidebarPage(id, QStringLiteral("list"), title, QStringLiteral("A page of %1.").arg(title));
	page->setScrollable(false);
	auto* list = new QListWidget;
	list->setObjectName(id + QStringLiteral("List"));
	for (int row = 0; row < 40; ++row) {
		list->addItem(QStringLiteral("%1 %2").arg(title).arg(row));
	}
	page->addSection(id + QStringLiteral("-items"), title, list, 1);
	auto* details = new QLabel(QStringLiteral("Details of the current row."));
	details->setWordWrap(true);
	page->addSection(id + QStringLiteral("-details"), QStringLiteral("Details"), details);
	return page;
}

SidebarPage* formPage(const QString& id, const QString& icon, const QString& title)
{
	auto* page = new SidebarPage(id, icon, title, QStringLiteral("Settings for %1.").arg(title));
	for (int index = 0; index < 3; ++index) {
		auto* body = new QWidget;
		auto* layout = new QVBoxLayout(body);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(new QLabel(QStringLiteral("Option %1").arg(index)));
		page->addSection(QStringLiteral("%1-%2").arg(id).arg(index), QStringLiteral("Group %1").arg(index), body);
	}
	return page;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true;

	QSplitter splitter(Qt::Horizontal);
	splitter.setChildrenCollapsible(false);
	auto* leading = new StudioSidebar(QStringLiteral("Browse"), StudioSidebar::TabEdge::Trailing);
	auto* centre = new QWidget;
	centre->setMinimumWidth(200);
	auto* trailing = new StudioSidebar(QStringLiteral("Properties"), StudioSidebar::TabEdge::Leading);
	splitter.addWidget(leading);
	splitter.addWidget(centre);
	splitter.addWidget(trailing);
	splitter.setStretchFactor(1, 1);

	leading->addPage(listPage(QStringLiteral("outliner"), QStringLiteral("Outliner")));
	leading->addPage(listPage(QStringLiteral("entities"), QStringLiteral("Entities")), true);
	leading->addPage(listPage(QStringLiteral("textures"), QStringLiteral("Textures")));
	trailing->addPage(formPage(QStringLiteral("item"), QStringLiteral("inspector"), QStringLiteral("Item")));
	trailing->addPage(formPage(QStringLiteral("view"), QStringLiteral("eye"), QStringLiteral("View")), true);

	splitter.resize(1200, 700);
	splitter.setSizes({300, 600, 300});
	splitter.show();
	settle();

	SidebarTabBar* bar = leading->sidebarTabBar();
	ok &= expect(leading->tabPosition() == QTabWidget::East, "trailing-edge tabs sit on the right left to right");
	ok &= expect(trailing->tabPosition() == QTabWidget::West, "leading-edge tabs sit on the left left to right");
	ok &= expect(leading->pageIds() == QStringList({QStringLiteral("outliner"), QStringLiteral("entities"), QStringLiteral("textures")}), "pages keep their order");
	ok &= expect(bar->tabText(1) == QStringLiteral("Entities") && bar->tabToolTip(1).contains(QStringLiteral("A page of Entities")),
		"tabs carry the title and description");
	ok &= expect(bar->startsGroup(1) && !bar->startsGroup(0) && !bar->startsGroup(2), "group starts are recorded per tab");
	ok &= expect(bar->tabRect(1).height() > bar->tabRect(2).height(), "a group start leaves a gap above its tile");
	ok &= expect(bar->width() == bar->columnWidth() && bar->columnWidth() >= 36, "the column has one width for every tab");
	ok &= expect(bar->tabAtPosition(QPoint(bar->width() / 2, bar->tabRect(1).top() + 2)) == -1, "the gap is not part of a tab");

	// Captions widen and lengthen the tiles.
	const int bare = bar->columnWidth();
	const int bareHeight = bar->tabRect(0).height();
	leading->setShowLabels(true);
	settle();
	ok &= expect(bar->columnWidth() > bare && bar->tabRect(0).height() > bareHeight, "captions add room under each glyph");
	leading->setShowLabels(false);
	settle();
	ok &= expect(bar->columnWidth() == bare, "captions off restores the narrow column");

	// Switching pages and folding.
	click(bar, tabCentre(bar, 2));
	ok &= expect(leading->currentPageId() == QStringLiteral("textures") && !leading->isFolded(), "a tab click opens its page");
	const int open = leading->width();
	const int centreOpen = centre->width();
	click(bar, tabCentre(bar, 2));
	ok &= expect(leading->isFolded(), "the current tab folds the sidebar");
	ok &= expect(leading->width() == leading->foldedWidth(), "a folded sidebar is as wide as its tab column");
	ok &= expect(centre->width() > centreOpen, "the work area takes the folded width");
	ok &= expect(!leading->page(QStringLiteral("textures"))->isVisible(), "a folded sidebar hides its page");
	click(bar, tabCentre(bar, 0));
	ok &= expect(!leading->isFolded() && leading->currentPageId() == QStringLiteral("outliner"), "another tab reopens the sidebar on its page");
	ok &= expect(std::abs(leading->width() - open) <= 2, "the sidebar reopens at its width");
	leading->setFolded(true);
	settle();
	leading->setCurrentWidget(leading->page(QStringLiteral("entities")));
	settle();
	ok &= expect(!leading->isFolded() && leading->page(QStringLiteral("entities"))->isVisible(), "a page chosen by code is shown");

	// Context menu requests name the tab.
	QString menuPage;
	QObject::connect(leading, &StudioSidebar::tabMenuRequested, [&menuPage](const QString& id, const QPoint&) { menuPage = id; });
	{
		const QPoint point = tabCentre(bar, 2);
		QContextMenuEvent menu(QContextMenuEvent::Mouse, point, bar->mapToGlobal(point));
		QApplication::sendEvent(bar, &menu);
	}
	ok &= expect(menuPage == QStringLiteral("textures"), "a right click asks for that tab's menu");

	// Sections: disclosure, keys and accessible state.
	SidebarPage* entities = leading->page(QStringLiteral("entities"));
	SidebarSection* items = entities->section(QStringLiteral("entities-items"));
	SidebarSection* details = entities->section(QStringLiteral("entities-details"));
	ok &= expect(items && details && items->isExpanded() && details->isExpanded(), "sections start open");
	ok &= expect(items->height() > details->height(), "a stretching section fills the page");
	QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(items->header());
	ok &= expect(accessible && accessible->role() == QAccessible::Button && accessible->state().expandable && accessible->state().expanded,
		"an open section header reports expanded");
	ok &= expect(accessible && accessible->text(QAccessible::Name) == QStringLiteral("Entities"), "a section header is named by its title");
	bool reported = false;
	QObject::connect(entities, &SidebarPage::sectionExpandedChanged, [&reported](const QString& id, bool open) {
		reported = id == QStringLiteral("entities-items") && !open;
	});
	click(items->header(), QPoint(20, items->header()->height() / 2));
	ok &= expect(!items->isExpanded() && !items->body()->isVisible() && reported, "a header click folds its section");
	ok &= expect(accessible && accessible->state().collapsed && !accessible->state().expanded, "a folded section header reports collapsed");
	ok &= expect(items->height() <= items->header()->height() + 4, "a folded section shrinks to its header");
	key(items->header(), Qt::Key_Right);
	ok &= expect(items->isExpanded(), "Right opens a section left to right");
	key(items->header(), Qt::Key_Left);
	ok &= expect(!items->isExpanded(), "Left folds a section left to right");
	key(items->header(), Qt::Key_Return);
	ok &= expect(items->isExpanded(), "Enter toggles a section");
	QToolButton* action = items->addHeaderButton(QStringLiteral("refresh"), QStringLiteral("Reload"), QStringLiteral("Reload the list."));
	ok &= expect(action && action->accessibleName() == QStringLiteral("Reload") && action->focusPolicy() == Qt::TabFocus,
		"section actions are named and reachable");

	// Page presentation follows into the tab.
	entities->setBadge(QStringLiteral("12"));
	entities->setTitle(QStringLiteral("Classes"));
	settle();
	ok &= expect(bar->tabText(1) == QStringLiteral("Classes") && bar->tabToolTip(1).contains(QStringLiteral("12")), "a retitled page renames its tab");

	// Moving a page to the other sidebar.
	SidebarPage* moved = leading->takePage(QStringLiteral("textures"));
	ok &= expect(moved && leading->indexOfPage(QStringLiteral("textures")) < 0, "a page can be taken out");
	trailing->addPage(moved, true);
	settle();
	ok &= expect(trailing->page(QStringLiteral("textures")) == moved && trailing->showPage(QStringLiteral("textures")) && moved->isVisible(),
		"a taken page shows in the other sidebar");

	// Painting: the current tab opens into its page; an icon is drawn.
	{
		trailing->showPage(QStringLiteral("item"));
		settle();
		SidebarTabBar* other = trailing->sidebarTabBar();
		const QImage image = other->grab().toImage();
		const QRect tab = other->tabRect(0);
		const QColor joined = image.pixelColor(other->width() - 2, tab.center().y() + tab.height() / 3);
		ok &= expect(joined == currentStudioTheme().colors.surface, "the current tile takes the page colour at the column edge");
		if (joined != currentStudioTheme().colors.surface) {
			std::cerr << "  joined pixel " << joined.name().toStdString() << " at " << other->width() - 2 << "," << tab.center().y() + tab.height() / 3
					  << " in a " << other->width() << "x" << other->height() << " column; tab " << tab.x() << "," << tab.y() << " " << tab.width() << "x" << tab.height() << '\n';
			const QString capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!capture.isEmpty()) { QDir().mkpath(capture); image.save(QDir(capture).filePath(QStringLiteral("sidebar-column.png"))); }
		}
		const QColor accent = image.pixelColor(2, tab.top() + tab.height() / 2);
		ok &= expect(accent == currentStudioTheme().colors.accent, "the current tab carries an accent mark on its outer edge");
	}

	// Right to left: the tab column mirrors.
	app.setLayoutDirection(Qt::RightToLeft);
	settle();
	ok &= expect(leading->tabPosition() == QTabWidget::West && trailing->tabPosition() == QTabWidget::East, "tab columns mirror right to left");
	key(items->header(), Qt::Key_Left);
	ok &= expect(items->isExpanded(), "Left opens a section right to left");
	key(items->header(), Qt::Key_Right);
	ok &= expect(!items->isExpanded(), "Right folds a section right to left");
	app.setLayoutDirection(Qt::LeftToRight);
	settle();

	// High contrast and large text keep the tiles and captions usable.
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
	settle();
	leading->setShowLabels(true);
	settle();
	ok &= expect(bar->columnWidth() > bare * 3 / 2, "large text scales the column");
	const QString capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!capture.isEmpty()) {
		QDir().mkpath(capture);
		ok &= expect(splitter.grab().save(QDir(capture).filePath(QStringLiteral("sidebar-hc-200.png"))), "capture saved");
	}

	std::cout << (ok ? "studio sidebar smoke passed" : "studio sidebar smoke failed") << "\n";
	return ok ? 0 : 1;
}
