#include "app/ui_primitives.h"
#include "app/studio_docks.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QScrollBar>
#include <QString>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QTabBar>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QVector>

#include <iostream>
#include <algorithm>
#include <memory>
#include <utility>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool hasPrimitive(const QVector<UiPrimitiveDescriptor>& primitives, const QString& id)
{
	for (const UiPrimitiveDescriptor& primitive : primitives) {
		if (primitive.id == id) {
			return true;
		}
	}
	return false;
}

class ThemeProbe final : public QWidget {
public:
	using QWidget::QWidget;
	int styleChanges = 0;
protected:
	void changeEvent(QEvent* event) override
	{
		if (event->type() == QEvent::StyleChange) { ++styleChanges; }
		QWidget::changeEvent(event);
	}
};

bool checkThemeTransitions(QApplication& app, DetailDrawer& drawer)
{
	bool ok = true;
	ThemeProbe root;
	QVector<ThemeProbe*> probes{&root};
	for (int depth = 0; depth < 24; ++depth) { probes.append(new ThemeProbe(probes.back())); }
	QLineEdit local(probes.back());
	const auto localStyle = QStringLiteral("QLineEdit { color: #234567; }");
	local.setStyleSheet(localStyle);
	local.setText(QStringLiteral("Retained text")); local.setSelection(1, 4);
	const auto theme = studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100);
	applyStudioTheme(app, theme);
	root.ensurePolished();
	for (auto* probe : probes) { probe->ensurePolished(); probe->styleChanges = 0; }
	applyStudioTheme(app, theme);
	for (const auto* probe : probes) {
		ok &= expect(probe->styleChanges == 0, "unchanged theme does not restyle nested widgets");
	}
	for (const auto next : {StudioTheme::HighContrastDark, StudioTheme::HighContrastLight, StudioTheme::Light, StudioTheme::Dark}) {
		for (auto* probe : probes) { probe->styleChanges = 0; }
		const auto tokens = studioThemeTokens(next, UiDensity::Standard, next == StudioTheme::Dark ? 100 : 200);
		applyStudioTheme(app, tokens); app.processEvents(QEventLoop::ExcludeUserInputEvents);
		int maximum = 0;
		for (const auto* probe : probes) {
			maximum = std::max(maximum, probe->styleChanges);
			ok &= expect(probe->styleChanges > 0 && probe->styleChanges <= 4,
				"theme change does bounded restyling at every widget depth");
			ok &= expect(qAbs(probe->font().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1
				&& probe->palette().color(QPalette::Window) == studioPalette(tokens).color(QPalette::Window),
				"nested widgets inherit the new theme font and palette");
		}
		std::cerr << "Theme transition " << themeId(next).toStdString() << " deepest_style_changes=" << probes.back()->styleChanges
			<< " maximum_style_changes=" << maximum << '\n';
		ok &= expect(local.styleSheet() == localStyle && local.palette().color(QPalette::Text) == QColor(QStringLiteral("#234567"))
			&& local.text() == QStringLiteral("Retained text") && local.selectedText() == QStringLiteral("etai"),
			"theme transition retains local styling, text and selection");
		auto* content = drawer.findChild<QTextEdit*>(QStringLiteral("detailContent"));
		ok &= expect(content && qAbs(content->font().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1
			&& qAbs(content->document()->defaultFont().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1
			&& drawer.currentSectionText() == QStringLiteral("Smoke test content"),
			"theme transitions retain the detail content and scale its fixed-pitch font");
	}
	return ok;
}

bool checkDetailRefresh(QApplication& app)
{
	bool ok = true;
	QStringList lines;
	for (int i = 0; i < 1000; ++i) { lines << QStringLiteral("Diagnostic row %1: retained content").arg(i); }
	const QString text = lines.join('\n');
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		DetailDrawer drawer; drawer.resize(680, 400);
		drawer.setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft);
		QVector<DetailSection> sections{{"summary", "Summary", "State", "Ready", OperationState::Completed},
			{"log", "Log", "Diagnostics", text, OperationState::Warning}};
		drawer.setSections(sections); drawer.showSection("log"); drawer.show();
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		auto* content = drawer.findChild<QTextEdit*>("detailContent");
		if (!expect(content != nullptr, "detail content exists")) { return false; }
		QTextCursor cursor(content->document()); cursor.setPosition(7); cursor.setPosition(24, QTextCursor::KeepAnchor);
		content->setTextCursor(cursor);
		auto* scroll = content->verticalScrollBar(); scroll->setValue(scroll->maximum() / 2);
		const int position = scroll->value();
		int changes = 0;
		const auto connection = QObject::connect(content->document(), &QTextDocument::contentsChanged, &drawer, [&] { ++changes; });
		auto* chooser = drawer.findChild<QListWidget*>("detailSections");
		if (!expect(chooser && chooser->count() == 2, "detail section chooser is available")) { return false; }
		const QPersistentModelIndex logRow = chooser->model()->index(1, 0);
		int resets = 0, updates = 0;
		const auto resetConnection = QObject::connect(chooser->model(), &QAbstractItemModel::modelReset, &drawer, [&] { ++resets; });
		const auto updateConnection = QObject::connect(chooser->model(), &QAbstractItemModel::dataChanged, &drawer, [&] { ++updates; });
		drawer.setSections(sections); drawer.showSection("log");
		ok &= expect(logRow.isValid() && resets == 0 && updates == 0,
			"repeated detail context keeps the reader's section row without redundant model notifications");
		sections[0].content = "New summary"; sections[0].summary = "New state"; sections[0].state = OperationState::Failed;
		drawer.setSections(sections); drawer.showSection("log");
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		ok &= expect(changes == 0 && drawer.currentSectionId() == "log" && drawer.currentSectionText() == text &&
			content->textCursor().selectedText() == cursor.selectedText() && scroll->value() == position && position > 0,
			"unchanged detail text keeps its selection and reading position during other context updates");
		ok &= expect(logRow.isValid() && logRow.data(Qt::UserRole) == "log" && resets == 0
			&& chooser->item(0)->data(Qt::AccessibleTextRole).toString().contains("New state")
			&& chooser->item(0)->data(Qt::UserRole + 1) == "failed",
			"changed detail metadata updates accessibly while keeping the active section row");
		sections[1].content = "Updated diagnostics"; drawer.setSections(sections);
		ok &= expect(changes > 0 && drawer.currentSectionId() == "log" && content->toPlainText() == "Updated diagnostics",
			"changed active content replaces the old diagnostic text");
		drawer.showSection("summary");
		ok &= expect(content->toPlainText() == "New summary", "switching sections displays current content");
		ok &= expect(logRow.isValid() && resets == 0, "section navigation preserves existing chooser rows");
		drawer.setSections({sections[1], sections[0]});
		ok &= expect(drawer.currentSectionId() == "summary" && chooser->currentRow() == 1
			&& content->toPlainText() == "New summary", "reordered sections retain the reader's section by identity");
		drawer.setSections({sections[1]});
		ok &= expect(drawer.currentSectionId() == "log" && content->toPlainText() == "Updated diagnostics",
			"removing the selected section chooses a valid remaining section");
		drawer.setSections({});
		ok &= expect(drawer.currentSectionId().isEmpty() && drawer.currentSectionText().isEmpty(), "empty details cannot retain stale content");
		drawer.setSections(sections); drawer.showSection("log");
		ok &= expect(content->toPlainText() == "Updated diagnostics", "reopening details after clear restores current content");
		QObject::disconnect(connection); QObject::disconnect(resetConnection); QObject::disconnect(updateConnection);
	}
	return ok;
}

// A notice's state edge and its wide margin sit on the leading side in both
// directions; a style sheet border-left would stay on the left in RTL.
bool checkNoticeEdge(QApplication& app)
{
	bool ok = true;
	for (const auto theme : {StudioTheme::Dark, StudioTheme::HighContrastLight}) {
		const auto tokens = studioThemeTokens(theme, UiDensity::Standard, 100);
		applyStudioTheme(app, tokens);
		int insets[2][2] = {};
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			NoticeBar notice;
			notice.setLayoutDirection(direction);
			notice.showNotice(QStringLiteral("failed"), QStringLiteral("The studio closed unexpectedly last time."),
				QStringLiteral("A report was kept; the files that were open were not reopened."));
			notice.addAction(QStringLiteral("View Report"));
			notice.resize(760, notice.sizeHint().height());
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			QImage image(notice.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			notice.render(&image);
			const int middle = image.height() / 2;
			const QRgb edge = tokens.colors.danger.rgb();
			const QRgb leading = image.pixel(mirrored ? image.width() - 1 : 0, middle);
			const QRgb trailing = image.pixel(mirrored ? 0 : image.width() - 1, middle);
			ok &= expect(leading == edge && trailing != edge, "a notice paints its state edge on its leading side only");
			auto* icon = notice.findChild<QLabel*>(QStringLiteral("noticeIcon"));
			auto* close = notice.findChild<QWidget*>(QStringLiteral("noticeClose"));
			if (!expect(icon && close, "notice glyph and close button exist")) {
				return false;
			}
			const auto leadingInset = [&](const QWidget* child) { return mirrored ? notice.width() - 1 - child->geometry().right() : child->x(); };
			const auto trailingInset = [&](const QWidget* child) { return mirrored ? child->x() : notice.width() - 1 - child->geometry().right(); };
			insets[mirrored][0] = leadingInset(icon);
			insets[mirrored][1] = trailingInset(close);
			ok &= expect(insets[mirrored][0] > insets[mirrored][1], "the notice's wide margin clears the edge on its leading side");
			const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			ok &= expect(root.isEmpty() || image.save(QDir(root).filePath(QStringLiteral("notice-%1-%2.png")
				.arg(themeId(theme), mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr")))), "write notice capture");
		}
		ok &= expect(insets[0][0] == insets[1][0] && insets[0][1] == insets[1][1],
			"a right-to-left notice mirrors the left-to-right spacing of its glyph and close button");
	}
	return ok;
}

// The columns painted top to bottom in one colour.
QVector<int> solidColumns(const QImage& image, QRgb color)
{
	QVector<int> columns;
	for (int x = 0; x < image.width(); ++x) {
		bool solid = true;
		for (int y = 0; solid && y < image.height(); ++y) {
			solid = image.pixel(x, y) == color;
		}
		if (solid) {
			columns.push_back(x);
		}
	}
	return columns;
}

// The rail's divider runs down the edge beside the page in both directions; a
// style sheet border-right would stay on the right, at the window's edge, in
// RTL. grab() paints as the screen does, starting each painter in the
// application's direction (left to right here) rather than the rail's.
bool checkRailDivider(QApplication& app)
{
	bool ok = true;
	const QVector<ModeRailEntry> entries{
		{0, QStringLiteral("Workspace"), QStringLiteral("Project health and recent work."), QStringLiteral("home")},
		{1, QStringLiteral("Levels"), QStringLiteral("Maps in an interactive viewport."), QStringLiteral("map"), true},
		{9, QStringLiteral("Settings"), QStringLiteral("Setup and preferences."), QStringLiteral("settings"), false, true},
	};
	for (const auto theme : {StudioTheme::Dark, StudioTheme::HighContrastLight}) {
		const auto tokens = studioThemeTokens(theme, UiDensity::Standard, 100);
		applyStudioTheme(app, tokens);
		int fromLeadingEdge[2] = {};
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			auto* rail = new ModeRail;
			rail->setEntries(entries);
			rail->setCurrentId(0);
			auto* page = new QWidget;
			RailHost host(rail, page);
			host.setLayoutDirection(direction);
			host.resize(320, 360);
			host.show();
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			const QImage image = host.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const QVector<int> columns = solidColumns(image, tokens.colors.borderSubtle.rgb());
			if (!expect(columns.size() == 1, "a rail paints one divider down its full height")) {
				return false;
			}
			const int divider = columns.first();
			ok &= expect(divider == (mirrored ? rail->geometry().left() : rail->geometry().right())
					&& divider == (mirrored ? page->geometry().right() + 1 : page->geometry().left() - 1),
				"the rail's divider sits on its trailing edge, beside the page");
			fromLeadingEdge[mirrored] = mirrored ? image.width() - 1 - divider : divider;
			const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			ok &= expect(root.isEmpty() || image.save(QDir(root).filePath(QStringLiteral("rail-%1-%2.png")
				.arg(themeId(theme), mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr")))), "write rail capture");
		}
		ok &= expect(fromLeadingEdge[0] == fromLeadingEdge[1], "a right-to-left rail mirrors the left-to-right divider column");
	}
	return ok;
}

// Where a glyph sits across the rail: the first and last painted columns of a
// widget's area, counted from the rail's outer edge and scanning inward until a
// gap ends the glyph (an open rail's label follows it). The divider down the
// rail's trailing edge is not scanned.
QPair<int, int> glyphColumns(const QImage& image, const QRect& area, const QRect& rail, bool mirrored, QRgb background)
{
	int first = -1;
	int last = -1;
	for (int offset = 0; offset < rail.width() - 1; ++offset) {
		const int x = mirrored ? rail.right() - offset : rail.left() + offset;
		if (x < area.left() || x > area.right()) {
			continue;
		}
		bool painted = false;
		for (int y = area.top(); !painted && y <= area.bottom(); ++y) {
			painted = image.pixel(x, y) != background;
		}
		if (painted) {
			first = first < 0 ? offset : first;
			last = offset;
		} else if (first >= 0 && offset - last > 3) {
			break;
		}
	}
	return {first, last};
}

QString describeColumns(const QVector<QPair<int, int>>& columns)
{
	QStringList spans;
	for (const auto& [first, last] : columns) {
		spans << QStringLiteral("%1..%2").arg(first).arg(last);
	}
	return spans.join(QStringLiteral(", "));
}

// The pin and the mode glyphs keep their distance from the rail's outer edge in
// both directions, folded and open, pinned or not: layout margins and style
// sheet padding do not mirror by themselves, and a centred icon's odd spare
// pixel rounds to the left. The application turns right to left too, as the
// studio does in Arabic, because a leading glyph aligns by the application's
// direction; mirroring only the rail would show offsets the studio never draws.
bool checkRailGlyphs(QApplication& app)
{
	bool ok = true;
	const QVector<ModeRailEntry> entries{
		{0, QStringLiteral("Workspace"), QStringLiteral("Project health and recent work."), QStringLiteral("home")},
		{1, QStringLiteral("Levels"), QStringLiteral("Maps in an interactive viewport."), QStringLiteral("map"), true},
		{9, QStringLiteral("Settings"), QStringLiteral("Setup and preferences."), QStringLiteral("settings"), false, true},
	};
	using Columns = QVector<QPair<int, int>>;
	for (const auto& [theme, scale] : {std::pair{StudioTheme::Dark, 100}, std::pair{StudioTheme::HighContrastLight, 200}}) {
		const auto tokens = studioThemeTokens(theme, UiDensity::Standard, scale);
		applyStudioTheme(app, tokens);
		const auto makeHost = [&]() {
			auto* rail = new ModeRail;
			rail->setReducedMotion(true);
			rail->setEntries(entries);
			rail->setCurrentId(0);
			auto host = std::make_unique<RailHost>(rail, new QWidget);
			host->resize(480, 400);
			host->show();
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			// Showing the window can hand the pin focus, which opens the rail
			// and rings the pin; this check wants neither.
			if (QWidget* focused = QApplication::focusWidget()) {
				focused->clearFocus();
			}
			return host;
		};
		// The pin first (its glyph, or its filled box once pinned), then the
		// glyph of each entry but the current one, which is filled and marked.
		// The window is too narrow for a pinned rail to stay open, so it folds
		// and opens as an automatic one does.
		const auto measure = [&](RailHost& host, int state, const QString& label) {
			const bool pinned = state / 2 != 0;
			const bool open = state % 2 != 0;
			auto* rail = host.findChild<ModeRail*>();
			rail->setBehaviour(pinned ? RailBehaviour::Expanded : RailBehaviour::Automatic);
			rail->setOpen(open);
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			const QImage image = host.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const bool mirrored = rail->layoutDirection() == Qt::RightToLeft;
			QVector<QWidget*> glyphs{rail->findChild<QWidget*>(QStringLiteral("railToggle"))};
			for (QToolButton* button : rail->findChildren<QToolButton*>(QStringLiteral("modeButton"))) {
				if (!button->isChecked()) {
					glyphs.push_back(button);
				}
			}
			Columns columns;
			for (const QWidget* glyph : glyphs) {
				columns.push_back(glyphColumns(image, QRect(glyph->mapTo(&host, QPoint()), glyph->size()), rail->geometry(), mirrored,
					tokens.colors.appBackground.rgb()));
			}
			const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			ok &= expect(root.isEmpty() || image.save(QDir(root).filePath(QStringLiteral("rail-glyphs-%1-%2-%3-%4-%5.png")
				.arg(themeId(theme)).arg(scale).arg(label, pinned ? QStringLiteral("pinned") : QStringLiteral("unpinned"),
					open ? QStringLiteral("open") : QStringLiteral("folded")))), "write rail glyph capture");
			return columns;
		};
		app.setLayoutDirection(Qt::LeftToRight);
		auto host = makeHost();
		Columns leftToRight[4];
		for (int state = 0; state < 4; ++state) {
			leftToRight[state] = measure(*host, state, QStringLiteral("ltr"));
			ok &= expect(leftToRight[state].size() == 3 && std::all_of(leftToRight[state].cbegin(), leftToRight[state].cend(),
					[](const QPair<int, int>& span) { return span.first > 0; }),
				"the rail paints its pin and each entry's glyph");
		}
		// Nothing moves away from the outer edge as the rail opens.
		for (const int pinned : {0, 2}) {
			for (int i = 0; i < leftToRight[pinned].size() && i < leftToRight[pinned + 1].size(); ++i) {
				ok &= expect(leftToRight[pinned][i].first == leftToRight[pinned + 1][i].first,
					"the pin and the mode glyphs keep their distance from the outer edge as the rail opens");
			}
		}
		// Turned right to left while showing, then built right to left as the
		// studio starts in Arabic: both mirror the left-to-right columns.
		app.setLayoutDirection(Qt::RightToLeft);
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		for (const bool rebuilt : {false, true}) {
			if (rebuilt) {
				host = makeHost();
			}
			const QString label = rebuilt ? QStringLiteral("rtl-built") : QStringLiteral("rtl-turned");
			for (int state = 0; state < 4; ++state) {
				const Columns rightToLeft = measure(*host, state, label);
				const bool mirrors = rightToLeft == leftToRight[state];
				ok &= expect(mirrors, "a right-to-left rail keeps its pin and glyphs as far from its outer edge as left to right");
				if (!mirrors) {
					std::cerr << "  " << themeId(theme).toStdString() << ' ' << scale << "% " << label.toStdString() << " state " << state
							  << ": left to right " << describeColumns(leftToRight[state]).toStdString() << ", right to left "
							  << describeColumns(rightToLeft).toStdString() << '\n';
				}
			}
		}
		host.reset();
		app.setLayoutDirection(Qt::LeftToRight);
	}
	return ok;
}

// Measure the asymmetric style sheet rules rather than assuming each Qt
// control uses physical padding in the same way. Menus and toolbar layouts
// already mirror their contents; a text-beside-icon tool button does not.
bool checkStyleSheetPadding(QApplication& app)
{
	bool ok = true;
	struct Surface {
		QToolBar bar;
		QMenu menu;
		QLineEdit search;
		QToolButton* run = nullptr;
		QAction* menuText = nullptr;
	};
	for (const auto& [theme, scale] : {std::pair{StudioTheme::Dark, 100}, std::pair{StudioTheme::HighContrastLight, 200}}) {
		AccessibilityPreferences preferences;
		preferences.theme = theme;
		preferences.textScalePercent = scale;
		preferences.thickFocusIndicator = true;
		preferences.steadyTextCursor = true;
		const auto tokens = studioThemeTokens(preferences);
		applyStudioTheme(app, tokens);
		QPixmap marker(16 * scale / 100, 16 * scale / 100);
		marker.fill(QColor(255, 0, 128));
		const auto makeSurface = [&]() {
			auto surface = std::make_unique<Surface>();
			surface->bar.setObjectName(QStringLiteral("studioToolBar"));
			surface->bar.setMovable(false);
			surface->bar.addAction(studioIcon(QStringLiteral("home")), QStringLiteral("Home"));
			surface->run = new QToolButton;
			surface->run->setProperty("runCommand", true);
			surface->run->setText(QStringLiteral("Launch Game"));
			surface->run->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
			// Colour isolates the real leading-aligned studio glyph from text.
			surface->run->setIcon(studioIcon(QStringLiteral("gamepad"), StudioIconTone::Accent, StudioIconAlignment::Leading));
			surface->run->setIconSize(QSize(24 * scale / 100, 18 * scale / 100));
			surface->bar.addWidget(surface->run);
			surface->bar.resize(800, surface->bar.sizeHint().height());
			surface->menu.addAction(QIcon(marker), QStringLiteral("||||||||\tCtrl+I"));
			surface->menuText = surface->menu.addAction(QStringLiteral("||||||||"));
			surface->menu.addMenu(QStringLiteral("Submenu"))->addAction(QStringLiteral("Child"));
			surface->search.setProperty("searchField", true);
			surface->search.setFixedWidth(400);
			surface->search.addAction(QIcon(marker), QLineEdit::LeadingPosition);
			return surface;
		};
		// Read painted bounds from the leading side, exactly as flipping an
		// Arabic snapshot horizontally before comparing it with English does.
		const auto columns = [](const QImage& image, const QRect& area, bool rtl, bool coloured, QRgb background) {
			int first = -1, last = -1;
			for (int offset = 0; offset < area.width(); ++offset) {
				const int x = rtl ? area.right() - offset : area.left() + offset;
				for (int y = area.top(); y <= area.bottom(); ++y) {
					const QRgb pixel = image.pixel(x, y);
					if (coloured ? std::abs(qRed(pixel) - qBlue(pixel)) > 20 : pixel != background) {
						first = first < 0 ? offset : first;
						last = offset;
						break;
					}
				}
			}
			return QPair(first, last);
		};
		const auto measure = [&](Surface& surface, const QString& label) {
			const bool rtl = app.layoutDirection() == Qt::RightToLeft;
			const auto capture = [&](QWidget& widget, const QString& name, bool focus = false) {
				widget.show();
				if (focus) { widget.activateWindow(); widget.setFocus(); }
				for (int pass = 0; pass < 3; ++pass) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
				if (!focus && app.focusWidget()) { app.focusWidget()->clearFocus(); }
				const QImage image = widget.grab().toImage();
				const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				ok &= expect(root.isEmpty() || image.save(QDir(root).filePath(QStringLiteral("padding-%1-%2-%3-%4.png")
					.arg(themeId(theme)).arg(scale).arg(label, name))), "write style sheet padding capture");
				return image;
			};
			QVector<QPair<int, int>> measured;
			const QImage bar = capture(surface.bar, QStringLiteral("toolbar"));
			for (QAction* action : surface.bar.actions()) {
				const QRect rect = QStyle::visualRect(app.layoutDirection(), surface.bar.rect(), surface.bar.actionGeometry(action));
				measured.push_back({rect.left(), rect.right()});
			}
			measured.push_back(columns(bar, surface.run->geometry(), rtl, true, 0));
			surface.bar.hide();
			surface.menu.popup(QPoint(20, 20));
			const QImage menu = capture(surface.menu, QStringLiteral("menu"));
			measured.push_back(columns(menu, menu.rect(), rtl, true, 0));
			measured.push_back(columns(menu, surface.menu.actionGeometry(surface.menuText), rtl, false, tokens.colors.panel.rgb()));
			surface.menu.hide();
			// Keep this window shown between the unfocused and focused samples.
			// The offscreen platform retains the hidden QWindow's focus identity,
			// so hide/show of that same window can elide the activation transition.
			// Clear/set widget focus instead; the focused assertion stays real.
			for (const bool focus : {false, true}) {
				const QImage search = capture(surface.search, focus ? QStringLiteral("search-focus") : QStringLiteral("search"), focus);
				QStyleOptionFrame option;
				option.initFrom(&surface.search);
				const QRect contents = QStyle::visualRect(app.layoutDirection(), surface.search.rect(),
					surface.search.style()->subElementRect(QStyle::SE_LineEditContents, &option, &surface.search));
				measured.push_back({contents.left(), contents.right()});
				if (!focus) { measured.push_back(columns(search, search.rect(), rtl, true, 0)); }
				ok &= expect(!focus || surface.search.hasFocus(), "measure the search field's thick focus padding while focused");
			}
			surface.search.hide();
			return measured;
		};
		app.setLayoutDirection(Qt::LeftToRight);
		auto surface = makeSurface();
		const auto ltr = measure(*surface, QStringLiteral("ltr"));
		ok &= expect(std::all_of(ltr.cbegin(), ltr.cend(), [](const auto& span) { return span.first >= 0 && span.second >= span.first; }),
			"the padding audit finds every content and glyph column");
		app.setLayoutDirection(Qt::RightToLeft);
		for (const bool rebuilt : {false, true}) {
			if (rebuilt) { surface = makeSurface(); }
			const auto rtl = measure(*surface, rebuilt ? QStringLiteral("rtl-startup") : QStringLiteral("rtl-live"));
			bool mirrors = rtl.size() == ltr.size();
			for (int index = 0; mirrors && index < ltr.size(); ++index) {
				// The menu label remains readable, not reflected: a glyph's
				// left and right ink bearings can differ by one raster pixel.
				const int tolerance = index == 4 ? 1 : 0;
				mirrors = std::abs(rtl[index].first - ltr[index].first) <= tolerance
					&& std::abs(rtl[index].second - ltr[index].second) <= tolerance;
			}
			ok &= expect(mirrors, "menus, toolbar children, run glyphs, and search fields keep their leading insets in RTL");
			if (!mirrors) {
				std::cerr << "  " << themeId(theme).toStdString() << ' ' << scale << "% " << (rebuilt ? "startup" : "live")
					<< ": LTR " << describeColumns(ltr).toStdString() << "; RTL " << describeColumns(rtl).toStdString() << '\n';
			}
		}
		app.setLayoutDirection(Qt::LeftToRight);
		ok &= expect(measure(*surface, QStringLiteral("ltr-return")) == ltr, "returning to LTR restores the original padding");
	}
	return ok;
}

// A panel's title bar keeps the title's wide margin on its leading side and its
// buttons near the trailing edge, in both directions and after the direction
// changes; layout margins do not mirror by themselves. Floating, its dock
// button shows a panel on the side the panels open on. The application turns,
// as an Arabic session starts, since a floating panel is a window of its own
// and takes the application's direction.
bool checkDockTitleMargins(QApplication& app)
{
	bool ok = true;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto dockButtonShows = [](const QWidget* bar, const QString& glyph) {
		for (const QToolButton* button : bar->findChildren<QToolButton*>(QStringLiteral("dockTitleButton"))) {
			if (button->toolTip().startsWith(QStringLiteral("Dock the"))) {
				const QSize size(16, 16);
				return button->icon().pixmap(size).toImage() == studioIcon(glyph).pixmap(size).toImage();
			}
		}
		return false;
	};
	// The title's gap from its own edge, then the nearest button's from the other.
	const auto measure = [](const QWidget* bar, bool mirrored, int insets[2]) {
		const auto* title = bar->findChild<QWidget*>(QStringLiteral("dockTitle"));
		const QList<QWidget*> buttons = bar->findChildren<QWidget*>(QStringLiteral("dockTitleButton"));
		if (!title || buttons.size() != 2) {
			return false;
		}
		insets[0] = mirrored ? bar->width() - 1 - title->geometry().right() : title->x();
		insets[1] = bar->width();
		for (const QWidget* button : buttons) {
			insets[1] = std::min(insets[1], mirrored ? button->x() : bar->width() - 1 - button->geometry().right());
		}
		return true;
	};
	int insets[2][2] = {};
	for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
		const bool mirrored = direction == Qt::RightToLeft;
		QGuiApplication::setLayoutDirection(direction);
		QMainWindow window;
		window.setCentralWidget(new QWidget);
		auto* dock = new QDockWidget(QStringLiteral("Activity"), &window);
		auto* bar = new DockTitleBar(dock);
		dock->setTitleBarWidget(bar);
		dock->setWidget(new QWidget);
		window.addDockWidget(trailingDockArea(direction), dock);
		window.resize(900, 600);
		window.show();
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		if (!expect(measure(bar, mirrored, insets[mirrored]), "a panel title bar has its title and two buttons")) {
			QGuiApplication::setLayoutDirection(Qt::LeftToRight);
			return false;
		}
		dock->setFloating(true);
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		ok &= expect(dockButtonShows(bar, trailingPanelGlyph(direction)), "a floating panel's dock button shows a panel on the trailing side");
		dock->setFloating(false);
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		// Turned the other way, as a running window would be.
		QGuiApplication::setLayoutDirection(mirrored ? Qt::LeftToRight : Qt::RightToLeft);
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		int turned[2] = {};
		ok &= expect(measure(bar, !mirrored, turned) && turned[0] == insets[mirrored][0] && turned[1] == insets[mirrored][1],
			"a panel title bar keeps its spacing when the layout direction changes");
	}
	QGuiApplication::setLayoutDirection(Qt::LeftToRight);
	ok &= expect(trailingPanelGlyph(Qt::LeftToRight) == QStringLiteral("sidebar-right") && trailingPanelGlyph(Qt::RightToLeft) == QStringLiteral("sidebar-left"),
		"the panel glyph shows a sidebar on the trailing side");
	ok &= expect(insets[0][0] > insets[0][1], "a panel title keeps the wider margin");
	ok &= expect(insets[0][0] == insets[1][0] && insets[0][1] == insets[1][1],
		"a right-to-left panel title bar mirrors the left-to-right title and button spacing");
	return ok;
}

// Panels keep their side relative to the reading direction (studio_docks): a
// saved window state mirrors with its panels' sizes, tab groups, current tabs,
// closed panels, rows, and corners; floating panels and tool bars stay where
// they are; mirroring twice gives back the same bytes; and bytes that are not
// a window state are left as they are.
bool checkDockStateMirroring(QApplication& app)
{
	bool ok = true;
	ok &= expect(trailingDockArea(Qt::LeftToRight) == Qt::RightDockWidgetArea && trailingDockArea(Qt::RightToLeft) == Qt::LeftDockWidgetArea,
		"panels open on the trailing side: the right left to right, the left right to left");
	const QStringList names{QStringLiteral("activity"), QStringLiteral("inspector"), QStringLiteral("assistant"), QStringLiteral("outline"),
		QStringLiteral("output"), QStringLiteral("problems"), QStringLiteral("console"), QStringLiteral("preview")};
	// Every panel docked on the right to begin with, as the shell builds its
	// own before restoring a state, and a tool bar along the top.
	const auto build = [&names](QMainWindow& window) {
		window.setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
		window.setCentralWidget(new QWidget);
		window.resize(1200, 800);
		for (const QString& name : names) {
			auto* dock = new QDockWidget(name, &window);
			dock->setObjectName(name);
			dock->setWidget(new QWidget);
			window.addDockWidget(Qt::RightDockWidgetArea, dock);
		}
		auto* tools = new QToolBar(QStringLiteral("tools"), &window);
		tools->setObjectName(QStringLiteral("tools"));
		tools->addAction(QStringLiteral("Build"));
		window.addToolBar(Qt::TopToolBarArea, tools);
	};
	const auto panel = [](QMainWindow& window, const char* name) { return window.findChild<QDockWidget*>(QLatin1String(name)); };
	const auto settle = [&app]() {
		for (int pass = 0; pass < 3; ++pass) {
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
		}
	};
	// The tabs, in order, of the tab group showing the named panel.
	const auto tabOrder = [](QMainWindow& window, const QString& name) {
		for (const QTabBar* bar : window.findChildren<QTabBar*>()) {
			QStringList tabs;
			for (int index = 0; index < bar->count(); ++index) {
				tabs << bar->tabText(index);
			}
			if (bar->isVisible() && tabs.contains(name)) {
				return tabs;
			}
		}
		return QStringList();
	};

	// What a user might make of them: Activity and Inspector tabbed on the
	// right with the Inspector in front and the Assistant closed below them,
	// an outline on the left, output beside a tab group of problems and a
	// console along the bottom, a floating preview, the left side taking the
	// bottom-left corner, and the tool bar moved to the left edge.
	QMainWindow saved;
	build(saved);
	saved.tabifyDockWidget(panel(saved, "activity"), panel(saved, "inspector"));
	saved.addDockWidget(Qt::LeftDockWidgetArea, panel(saved, "outline"));
	saved.addDockWidget(Qt::BottomDockWidgetArea, panel(saved, "output"));
	saved.addDockWidget(Qt::BottomDockWidgetArea, panel(saved, "problems"));
	saved.tabifyDockWidget(panel(saved, "problems"), panel(saved, "console"));
	saved.setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	saved.addToolBar(Qt::LeftToolBarArea, saved.findChild<QToolBar*>(QStringLiteral("tools")));
	saved.show();
	settle();
	panel(saved, "inspector")->raise();
	panel(saved, "console")->raise();
	panel(saved, "assistant")->hide();
	panel(saved, "preview")->setFloating(true);
	panel(saved, "preview")->setGeometry(40, 40, 240, 180);
	saved.resizeDocks({panel(saved, "inspector"), panel(saved, "outline")}, {360, 220}, Qt::Horizontal);
	saved.resizeDocks({panel(saved, "output")}, {300}, Qt::Horizontal);
	settle();

	const QByteArray state = saved.saveState();
	const std::optional<QByteArray> mirrored = mirroredWindowState(state);
	if (!expect(mirrored.has_value() && *mirrored != state, "a saved window state with docked panels mirrors")) {
		return false;
	}
	ok &= expect(mirroredWindowState(*mirrored) == state, "mirroring a window state twice gives back the same bytes");
	ok &= expect(windowStateForDirection(state, Qt::LeftToRight) == state && windowStateForDirection(state, Qt::RightToLeft) == *mirrored,
		"a window state is mirrored for a right-to-left window and kept as it is for a left-to-right one");

	// Restored as the next session would: built, restored, then shown.
	QMainWindow restored;
	build(restored);
	ok &= expect(restored.restoreState(*mirrored), "Qt restores a mirrored window state");
	restored.show();
	settle();
	const auto area = [&restored, &panel](const char* name) { return restored.dockWidgetArea(panel(restored, name)); };
	ok &= expect(area("activity") == Qt::LeftDockWidgetArea && area("inspector") == Qt::LeftDockWidgetArea && area("assistant") == Qt::LeftDockWidgetArea
			&& area("outline") == Qt::RightDockWidgetArea && area("output") == Qt::BottomDockWidgetArea && area("problems") == Qt::BottomDockWidgetArea,
		"the left and right panels trade sides; the bottom ones stay at the bottom");
	const QRect central = restored.centralWidget()->geometry();
	QDockWidget* inspector = panel(restored, "inspector");
	ok &= expect(inspector->geometry().right() < central.left() && panel(restored, "outline")->geometry().left() > central.right(),
		"the mirrored panels sit on the other sides of the work area");
	ok &= expect(restored.tabifiedDockWidgets(inspector).contains(panel(restored, "activity")) && inspector->geometry().right() >= 0
			&& panel(restored, "activity")->geometry().right() < 0,
		"Activity and Inspector stay tabbed together with the Inspector in front");
	ok &= expect(inspector->width() == panel(saved, "inspector")->width() && panel(restored, "outline")->width() == panel(saved, "outline")->width(),
		"each side's panels keep their widths");
	QDockWidget* output = panel(restored, "output");
	QDockWidget* console = panel(restored, "console");
	ok &= expect(panel(saved, "output")->x() < panel(saved, "console")->x() && output->x() > console->x()
			&& output->width() == panel(saved, "output")->width() && console->width() == panel(saved, "console")->width(),
		"panels side by side along the bottom swap places and keep their widths");
	ok &= expect(restored.tabifiedDockWidgets(console).contains(panel(restored, "problems")) && console->geometry().right() >= 0
			&& panel(restored, "problems")->geometry().right() < 0
			&& tabOrder(restored, QStringLiteral("console")) == tabOrder(saved, QStringLiteral("console"))
			&& tabOrder(saved, QStringLiteral("console")).size() == 2,
		"a tab group along the bottom keeps its tabs in order and the console in front");
	ok &= expect(restored.corner(Qt::BottomRightCorner) == Qt::RightDockWidgetArea && restored.corner(Qt::BottomLeftCorner) == Qt::BottomDockWidgetArea,
		"a corner taken by one side passes to the other side's corner");
	ok &= expect(restored.toolBarArea(restored.findChild<QToolBar*>(QStringLiteral("tools"))) == Qt::LeftToolBarArea, "a tool bar stays where it was");
	QDockWidget* preview = panel(restored, "preview");
	ok &= expect(preview->isFloating() && preview->geometry() == panel(saved, "preview")->geometry(), "a floating panel stays where it was on the screen");
	QDockWidget* assistant = panel(restored, "assistant");
	ok &= expect(assistant->isHidden(), "a closed panel stays closed");
	assistant->show();
	preview->setFloating(false);
	settle();
	ok &= expect(assistant->geometry().top() > inspector->geometry().top() && restored.dockWidgetArea(preview) == Qt::LeftDockWidgetArea,
		"a closed panel reopens below its neighbours, and a floating one docks back, on the mirrored side");

	const QByteArray notState("state-bytes");
	// QMainWindow's own marker, an int written big-endian, which restoreState()
	// refuses too when it is wrong.
	QByteArray otherMarker = state;
	otherMarker[3] = '\x7f';
	ok &= expect(!mirroredWindowState(notState) && !mirroredWindowState(QByteArray()) && !mirroredWindowState(state.left(state.size() / 2))
			&& !mirroredWindowState(otherMarker),
		"bytes that are not a whole window state are not mirrored");
	ok &= expect(windowStateForDirection(notState, Qt::RightToLeft) == notState, "a state that cannot be read is kept as it is");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	bool ok = true;

	const QVector<UiPrimitiveDescriptor> primitives = uiPrimitiveDescriptors();
	ok &= expect(primitives.size() >= 2, "expected at least two UI primitive descriptors");
	ok &= expect(hasPrimitive(primitives, QStringLiteral("loading-pane")), "missing loading-pane descriptor");
	ok &= expect(hasPrimitive(primitives, QStringLiteral("detail-drawer")), "missing detail-drawer descriptor");

	for (const UiPrimitiveDescriptor& primitive : primitives) {
		ok &= expect(!primitive.id.trimmed().isEmpty(), "primitive id is empty");
		ok &= expect(!primitive.title.trimmed().isEmpty(), "primitive title is empty");
		ok &= expect(!primitive.description.trimmed().isEmpty(), "primitive description is empty");
		ok &= expect(!primitive.useCases.isEmpty(), "primitive use cases are empty");
	}

	LoadingPane loadingPane;
	loadingPane.setState(OperationState::Loading);
	loadingPane.setPlaceholderRows({QStringLiteral("First"), QStringLiteral("Second")});
	loadingPane.setPlaceholderRows({QStringLiteral("Project manifest"), QStringLiteral("Diagnostics")});
	loadingPane.setPlaceholderRows({});
	ok &= expect(loadingPane.placeholderRows().isEmpty(), "empty placeholder rows should use default rendered rows without storing them");

	DetailDrawer drawer;
	drawer.setSections({
		{
			QStringLiteral("summary"),
			QStringLiteral("Summary"),
			QStringLiteral("Smoke test summary"),
			QStringLiteral("Smoke test content"),
			OperationState::Completed,
		},
	});
	drawer.showSection(QStringLiteral("summary"));
	ok &= expect(drawer.currentSectionText() == QStringLiteral("Smoke test content"), "detail drawer should expose selected section content");
	drawer.show();
	app.processEvents(QEventLoop::ExcludeUserInputEvents);
	auto* content = drawer.findChild<QTextEdit*>(QStringLiteral("detailContent"));
	for (const qreal points : {10.5, 21.0, 10.5}) {
		QFont font = app.font(); font.setPointSizeF(points); app.setFont(font);
		app.setStyleSheet(QStringLiteral("QFrame { background: #101010; color: #ffffff; border-radius: %1px; }").arg(points));
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		ok &= expect(content && qAbs(content->font().pointSizeF() - points) < 0.1
			&& qAbs(content->document()->defaultFont().pointSizeF() - points) < 0.1,
			"detail text follows live application font and stylesheet changes in both directions");
	}

	ok &= checkThemeTransitions(app, drawer);
	ok &= checkDetailRefresh(app);
	ok &= checkNoticeEdge(app);
	ok &= checkRailDivider(app);
	ok &= checkRailGlyphs(app);
	ok &= checkStyleSheetPadding(app);
	ok &= checkDockTitleMargins(app);
	ok &= checkDockStateMirroring(app);
	return ok ? 0 : 1;
}
