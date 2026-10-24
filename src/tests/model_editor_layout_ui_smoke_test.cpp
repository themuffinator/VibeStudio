// The Mesh Editor as the studio hosts it, in an editor page, at a laptop page
// (1280x720 and 1366x768) and a desktop one (1600x1000): at 100% text in the
// dark theme, and at 200% text in both high-contrast themes with every app
// string a third longer, left to right and right to left. The editor fits the
// page without scrolling it, the views keep most of the room in one and four
// views for every profile and with every sidebar page open, the chrome around
// the sidebars stays within four text lines, no visible label, button or check
// box is cut short, the profile's timeline flag applies, the tool shelf's
// icons follow the text scale and a status line cut short can be read from
// the keyboard. In Material shading, finished and while images load, the
// material images' state sits in the status strip: the views keep the same
// room, the chrome stays within four lines, the 3D and UV tabs stay whole and
// none of its actions is cut short. A wrapping check box toggles from its
// indicator and caption only, not from the blank rest of a stretched row.
// The modeller's dialogs are then opened at 200% text on a 1280x720 screen:
// each fits it, with its buttons on the screen, its body scrolling down
// rather than sideways and no shown control cut short.
//
// VIBESTUDIO_MODELLER_EVIDENCE=<dir> saves a render of each state;
// VIBESTUDIO_LAYOUT_REPORT=1 prints the measurements.
#include "app/dialog_screen_fit.h"
#include "app/model_animation_curves_dialog.h"
#include "app/model_assembly_dialog.h"
#include "app/model_controls_dialog.h"
#include "app/model_curve_keys_dialog.h"
#include "app/model_curve_transfer_dialog.h"
#include "app/model_design_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_ik_dialog.h"
#include "app/model_import_repair_dialog.h"
#include "app/model_material_slots_dialog.h"
#include "app/model_md5_export_dialog.h"
#include "app/model_q3_animation_dialog.h"
#include "app/model_rig_dialog.h"
#include "app/model_skeletal_dialog.h"
#include "app/model_skin_source_dialog.h"
#include "app/model_skinning_dialog.h"
#include "app/model_surface_dialog.h"
#include "app/model_uv_density_dialog.h"
#include "app/model_viewport.h"
#include "app/model_weight_paint_dialog.h"
#include "app/model_weight_transfer_dialog.h"
#include "app/status_line_label.h"
#include "app/studio_editor_page.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "app/wrapping_check_box.h"
#include "core/model_design.h"
#include "core/model_import_repair.h"
#include "core/studio_settings.h"
#include "tests/model_curve_keys_test_helpers.h"
#include "tests/model_import_repair_test_helpers.h"
#include "tests/model_q3_animation_test_helpers.h"
#include "tests/model_uv_density_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_weight_transfer_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool condition, const std::string &message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
bool reporting()
{
	return qEnvironmentVariableIntValue("VIBESTUDIO_LAYOUT_REPORT") != 0;
}
// Every studio string a third longer, in words as German or Finnish run
// longer, never in one unbreakable run. Qt's own contexts stay as they are:
// key names are parsed back from them.
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (!context || context[0] == 'Q')
			return {};
		const auto original = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(original, QStringLiteral("~~ ").repeated((original.size() + 8) / 9).trimmed());
	}
};
struct Variant
{
	const char *name;
	StudioTheme theme;
	int scale;
	bool rightToLeft;
	// Every studio string a third longer (Expansion).
	bool expanded;
};
// UTF-8 text for messages, copied here: a std::string from toStdString() is
// made inside the Qt library, which a debug Qt build frees with another heap.
std::string narrow(const QString &text)
{
	return std::string(text.toUtf8().constData());
}
QString describe(const QWidget *widget)
{
	QString text;
	if (const auto *label = qobject_cast<const QLabel *>(widget))
		text = label->text();
	else if (const auto *button = qobject_cast<const QAbstractButton *>(widget))
		text = button->text();
	return QString::fromLatin1(widget->metaObject()->className()) + QStringLiteral(" ") + widget->objectName() + QStringLiteral(" \"") +
		   text.left(48) + QStringLiteral("\"");
}
// Shown controls whose text does not fit the room they were given. Labels
// that elide by design (their whole text in the tooltip) are left out.
QStringList clippedControls(QWidget *root)
{
	QStringList found;
	const auto add = [&found](QWidget *widget, const QSize &needed)
	{
		found << QStringLiteral("%1 (%2x%3 in %4x%5)")
					 .arg(describe(widget))
					 .arg(needed.width())
					 .arg(needed.height())
					 .arg(widget->width())
					 .arg(widget->height());
	};
	for (auto *label : root->findChildren<QLabel *>())
	{
		if (!label->isVisible() || label->text().trimmed().isEmpty() || dynamic_cast<ElidedLabel *>(label) ||
			dynamic_cast<StatusLineLabel *>(label))
			continue;
		if (label->wordWrap())
		{
			const int needed = label->heightForWidth(label->width());
			if (needed > label->height() + 1)
			{
				add(label, {label->width(), needed});
				if (qEnvironmentVariableIntValue("VIBESTUDIO_LAYOUT_REPORT") >= 3 && label->parentWidget())
				{
					auto *parent = label->parentWidget();
					std::cout << "  label hfw " << label->hasHeightForWidth() << " parent " << narrow(describe(parent)) << " "
							  << parent->width() << "x" << parent->height() << " hfw " << parent->hasHeightForWidth() << " -> "
							  << parent->heightForWidth(parent->width()) << " layout "
							  << (parent->layout() ? parent->layout()->metaObject()->className() : "none") << '\n';
				}
			}
		}
		else if (label->sizeHint().width() > label->width() + 1 || label->sizeHint().height() > label->height() + 1)
			add(label, label->sizeHint());
	}
	for (auto *button : root->findChildren<QAbstractButton *>())
	{
		if (!button->isVisible() || button->text().trimmed().isEmpty() ||
			(!qobject_cast<QPushButton *>(button) && !qobject_cast<QCheckBox *>(button)))
			continue;
		if (button->hasHeightForWidth())
		{
			const int needed = button->heightForWidth(button->width());
			if (needed > button->height() + 1)
				add(button, {button->width(), needed});
		}
		else if (button->sizeHint().width() > button->width() + 1)
			add(button, button->sizeHint());
	}
	return found;
}
// With VIBESTUDIO_LAYOUT_REPORT=2, the shown widgets whose minimum takes more
// than a third of the room, to find what holds a layout open.
void reportMinimums(QWidget *root, const QSize &room)
{
	if (qEnvironmentVariableIntValue("VIBESTUDIO_LAYOUT_REPORT") < 2)
		return;
	if (auto *layout = root->layout())
	{
		for (int index = 0; index < layout->count(); ++index)
		{
			const QSize minimum = layout->itemAt(index)->minimumSize();
			std::cout << "  row " << index << " minimum " << minimum.width() << "x" << minimum.height()
					  << (layout->itemAt(index)->widget() ? ": " + narrow(describe(layout->itemAt(index)->widget())) : std::string(" (layout)")) << '\n';
		}
	}
	for (auto *widget : root->findChildren<QWidget *>())
	{
		if (!widget->isVisible())
			continue;
		const QSize minimum = widget->minimumSize().expandedTo(widget->minimumSizeHint());
		if (minimum.width() * 3 > room.width() || minimum.height() * 3 > room.height())
			std::cout << "  minimum " << minimum.width() << "x" << minimum.height() << ": " << narrow(describe(widget)) << '\n';
	}
}
bool capture(QWidget &widget, const QString &name)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
		return true;
	return QDir().mkpath(directory) && widget.grab().save(QDir(directory).filePath(name + QStringLiteral(".png")));
}
ModelMesh animatedPlane()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	// A second frame, so the timeline has something to step through.
	mesh.frames << mesh.frames.first();
	mesh.frames.last().name = QStringLiteral("frame001");
	for (auto &surface : mesh.surfaces)
		surface.frames << surface.frames.first();
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// One editor in a page of `size`, measured in each profile and layout.
void checkEditor(QApplication &app, const Variant &variant, const QSize &size)
{
	const std::string where = std::string(variant.name) + " " + std::to_string(size.width()) + "x" + std::to_string(size.height());
	QTabWidget host;
	host.setObjectName(QStringLiteral("modelsWorkspace"));
	host.addTab(new QWidget, QStringLiteral("Models"));
	host.resize(size);
	auto *editor = new ModelEditorDialog;
	QString error;
	expect(editor->setMesh(animatedPlane(), &error), where + ": load a two-frame plane");
	auto *page = new StudioEditorPage(editor, &host);
	host.setCurrentWidget(page);
	host.show();
	editor->show();
	app.processEvents();
	app.processEvents();
	auto *splitter = editor->findChild<QSplitter *>(QStringLiteral("meshEditingControls"));
	auto *views = editor->findChild<QWidget *>(QStringLiteral("meshViewPanes"));
	auto *shelf = editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf"));
	if (!expect(splitter && views && shelf && editor->tools(), where + ": the editor has its splitter, views and shelf"))
		return;
	expect(shelf->iconSize() == scaledIconSize(QSize(20, 20)), where + ": the tool shelf's icons follow the text scale");
	const int line = editor->fontMetrics().lineSpacing();
	const struct
	{
		const char *profile;
		ModelViewLayout layout;
	} states[]{{"studio", ModelViewLayout::Single},		{"studio", ModelViewLayout::FourViews}, {"blender", ModelViewLayout::Single},
			   {"3ds-max", ModelViewLayout::FourViews}, {"milkshape-3d", ModelViewLayout::FourViews}};
	for (const auto &state : states)
	{
		const std::string at = where + " " + state.profile + (state.layout == ModelViewLayout::FourViews ? " four views" : " one view");
		editor->applyProfile(QString::fromLatin1(state.profile), false);
		editor->setViewLayout(state.layout);
		app.processEvents();
		app.processEvents();
		const QSize minimum = editor->minimumSizeHint();
		const QSize room = page->viewport()->size();
		const bool scrolls = page->horizontalScrollBar()->isVisible() || page->verticalScrollBar()->isVisible();
		const double wide = double(views->width()) / editor->width(), tall = double(views->height()) / editor->height();
		const int chrome = editor->height() - splitter->height();
		const auto clipped = clippedControls(editor);
		if (reporting())
		{
			std::cout << at << ": page " << room.width() << "x" << room.height() << ", editor minimum " << minimum.width() << "x"
					  << minimum.height() << ", views " << views->width() << "x" << views->height() << " (" << int(wide * 100) << "% x "
					  << int(tall * 100) << "%), chrome " << chrome << " px = " << double(chrome) / line << " lines, sidebars "
					  << editor->leadingSidebar()->width() << "/" << editor->trailingSidebar()->width() << " (minimum "
					  << editor->leadingSidebar()->minimumSizeHint().width() << "/" << editor->trailingSidebar()->minimumSizeHint().width()
					  << ")\n";
			for (const auto &entry : clipped)
				std::cout << "  clipped: " << narrow(entry) << '\n';
			reportMinimums(editor, room);
		}
		expect(!scrolls && minimum.width() <= room.width() && minimum.height() <= room.height(),
			   at + ": the editor fits the page without scrolling it (minimum " + std::to_string(minimum.width()) + "x" +
				   std::to_string(minimum.height()) + ")");
		expect(wide >= 0.45, at + ": the views keep at least 45% of the width (" + std::to_string(int(wide * 100)) + "%)");
		expect(tall >= 0.5, at + ": the views keep at least half the height (" + std::to_string(int(tall * 100)) + "%)");
		expect(chrome <= 4 * line, at + ": the chrome above and below the panels is at most four text lines (" + std::to_string(chrome) +
									   " px, line " + std::to_string(line) + ")");
		expect(clipped.isEmpty(), at + ": no shown control is cut short" + (clipped.isEmpty() ? "" : ": " + narrow(clipped.first())));
		for (auto *pane : editor->panes())
		{
			if (pane && pane->isVisible() && !qEnvironmentVariableIsEmpty("VIBESTUDIO_MODELLER_EVIDENCE"))
				tests::settleModelViewport(*pane);
		}
		expect(capture(host, QStringLiteral("layout-%1-%2x%3-%4-%5")
								 .arg(QString::fromLatin1(variant.name))
								 .arg(size.width())
								 .arg(size.height())
								 .arg(QString::fromLatin1(state.profile))
								 .arg(state.layout == ModelViewLayout::FourViews ? QStringLiteral("four") : QStringLiteral("one"))),
			   at + ": save the render");
	}
	// Back from four views, a one-view profile shows its one view.
	editor->applyProfile(QStringLiteral("studio"), false);
	app.processEvents();
	int shownPanes = 0;
	for (auto *pane : editor->panes())
		shownPanes += pane && pane->isVisible();
	expect(editor->viewLayout() == ModelViewLayout::Single && shownPanes == 1,
		   where + ": a one-view profile after four views shows one view (" + std::to_string(shownPanes) + " shown)");
	// Material shading shows the material images' state in the status strip,
	// finished and while images load (its bar shown, Cancel on): the views
	// keep their room, the chrome stays within four lines, the 3D and UV tabs
	// stay whole and the line's actions are not cut short.
	auto *render = editor->findChild<QComboBox *>(QStringLiteral("meshRenderMode"));
	auto *materialLine = editor->findChild<QWidget *>(QStringLiteral("meshMaterialState"));
	auto *materialBar = editor->findChild<QProgressBar *>(QStringLiteral("meshMaterialProgress"));
	auto *materialCancel = editor->findChild<QToolButton *>(QStringLiteral("cancelMeshMaterials"));
	auto *viewTabs = editor->findChild<QTabWidget *>(QStringLiteral("meshViewTabs"));
	if (expect(render && materialLine && materialBar && materialCancel && viewTabs && render->count() > 3,
			   where + ": the editor has Material shading and its line"))
	{
		render->setCurrentIndex(3);
		QElapsedTimer waiting;
		waiting.start();
		while (editor->materialLoading() && waiting.elapsed() < 20000)
			app.processEvents(QEventLoop::AllEvents, 20);
		for (const bool loading : {false, true})
		{
			// As while images load: the bar and Cancel beside the line.
			materialBar->setVisible(loading);
			materialCancel->setVisible(loading);
			app.processEvents();
			app.processEvents();
			const std::string at = where + (loading ? ": Material shading while images load" : ": Material shading");
			const QSize minimum = editor->minimumSizeHint();
			const QSize room = page->viewport()->size();
			const bool scrolls = page->horizontalScrollBar()->isVisible() || page->verticalScrollBar()->isVisible();
			const double wide = double(views->width()) / editor->width(), tall = double(views->height()) / editor->height();
			const int chrome = editor->height() - splitter->height();
			auto clipped = clippedControls(editor);
			// Its actions are tool buttons, which clippedControls passes over.
			for (auto *button : materialLine->findChildren<QToolButton *>())
			{
				const QSize hint = button->sizeHint();
				if (button->isVisible() && (button->width() + 1 < hint.width() || button->height() + 1 < hint.height()))
					clipped << QStringLiteral("%1 %2x%3 of %4x%5")
								   .arg(describe(button))
								   .arg(button->width())
								   .arg(button->height())
								   .arg(hint.width())
								   .arg(hint.height());
			}
			// The view tabs keep their tabs whole beside nothing else.
			const QRect tabs = viewTabs->tabBar()->geometry();
			const bool tabsWhole = viewTabs->tabBar()->isVisible() && tabs.width() >= viewTabs->tabBar()->sizeHint().width() &&
								   viewTabs->tabBar()->tabRect(viewTabs->count() - 1).right() <= tabs.width();
			const bool inStrip = materialLine->mapTo(editor, QPoint(0, 0)).y() >= splitter->geometry().bottom();
			if (reporting())
			{
				std::cout << at << ": views " << int(wide * 100) << "% x " << int(tall * 100) << "%, chrome " << chrome
						  << " px, material line " << materialLine->width() << "x" << materialLine->height() << '\n';
				for (const auto &entry : clipped)
					std::cout << "  clipped: " << narrow(entry) << '\n';
			}
			auto *images = materialLine->findChild<QToolButton *>(QStringLiteral("meshMaterialActions"));
			expect(materialLine->isVisible() && inStrip && images && images->isVisible() && materialCancel->isVisible() == loading,
				   at + ": the material line is shown in the status strip under the panels, with its Images menu");
			expect(!scrolls && minimum.width() <= room.width() && minimum.height() <= room.height(),
				   at + ": the editor fits the page without scrolling it (minimum " + std::to_string(minimum.width()) + "x" +
					   std::to_string(minimum.height()) + ")");
			expect(wide >= 0.45 && tall >= 0.5, at + ": the views keep at least 45% of the width and half the height (" +
													 std::to_string(int(wide * 100)) + "% x " + std::to_string(int(tall * 100)) + "%)");
			expect(chrome <= 4 * line, at + ": the chrome stays within four text lines (" + std::to_string(chrome) + " px)");
			expect(tabsWhole, at + ": the 3D and UV tabs stay whole");
			expect(clipped.isEmpty(), at + ": no shown control is cut short" + (clipped.isEmpty() ? "" : ": " + narrow(clipped.first())));
			if (size == QSize(1280, 720))
				expect(capture(host, QStringLiteral("material-%1-%2").arg(QString::fromLatin1(variant.name))
										 .arg(loading ? QStringLiteral("loading") : QStringLiteral("shown"))),
					   at + ": save the render");
		}
		materialBar->hide();
		materialCancel->hide();
		render->setCurrentIndex(0);
		app.processEvents();
		expect(!materialLine->isVisible(), where + ": the material line goes with Material shading");
	}
	// Every sidebar page, shown in turn, fits its sidebar.
	for (auto *sidebar : {editor->leadingSidebar(), editor->trailingSidebar()})
	{
		for (const auto &id : sidebar->pageIds())
		{
			editor->showSidebarPage(id);
			// The views are refitted once the editor is laid out again.
			app.processEvents();
			app.processEvents();
			auto *sidebarPage = editor->sidebarPage(id);
			auto *scroll = tests::pageScroll(sidebarPage);
			const auto clipped = clippedControls(sidebarPage);
			// The page opened stays open, and the views keep their room: the
			// other sidebar folds rather than the views shrinking to a strip.
			const double share = double(views->width()) / editor->width();
			if (reporting())
				std::cout << where << " page " << narrow(id) << ": views " << int(share * 100) << "%, sidebars "
						  << editor->leadingSidebar()->width() << (editor->leadingSidebar()->isFolded() ? " folded" : "") << "/"
						  << editor->trailingSidebar()->width() << (editor->trailingSidebar()->isFolded() ? " folded" : "") << '\n';
			expect(!sidebar->isFolded() && sidebarPage->isVisible(), where + ": the " + narrow(id) + " page is open once shown");
			expect(share >= 0.45, where + ": with the " + narrow(id) + " page open the views keep at least 45% of the width (" +
									  std::to_string(int(share * 100)) + "%)");
			// Opening a sidebar never makes the editor outgrow its page.
			const bool outgrown = page->horizontalScrollBar()->isVisible() || page->verticalScrollBar()->isVisible();
			if (outgrown && reporting())
			{
				std::cout << where << " page " << narrow(id) << " outgrows: editor minimum " << editor->minimumSizeHint().width() << "x"
						  << editor->minimumSizeHint().height() << " in " << page->viewport()->width() << "x" << page->viewport()->height()
						  << ", sidebars " << editor->leadingSidebar()->width() << (editor->leadingSidebar()->isFolded() ? " folded" : "") << "/"
						  << editor->trailingSidebar()->width() << (editor->trailingSidebar()->isFolded() ? " folded" : "") << '\n';
				reportMinimums(editor, page->viewport()->size());
			}
			expect(!outgrown, where + ": showing the " + narrow(id) + " page keeps the editor within its page");
			if (size == QSize(1280, 720))
				expect(capture(host, QStringLiteral("page-%1-%2").arg(QString::fromLatin1(variant.name), id)), where + ": save the page render");
			if (qEnvironmentVariableIntValue("VIBESTUDIO_LAYOUT_REPORT") >= 3 && scroll && scroll->widget())
			{
				std::cout << where << " page " << narrow(id) << ": content minimum " << scroll->widget()->minimumSizeHint().width() << '\n';
				for (auto *child : scroll->widget()->findChildren<QWidget *>())
				{
					const int minimum = child->minimumSize().expandedTo(child->minimumSizeHint()).width();
					if (child->isVisible() && child->findChildren<QWidget *>().isEmpty() && minimum * 10 > scroll->widget()->minimumSizeHint().width() * 8)
						std::cout << "    " << minimum << ": " << narrow(describe(child)) << '\n';
				}
			}
			if (reporting() && (!clipped.isEmpty() || (scroll && scroll->horizontalScrollBar()->isVisible())))
			{
				std::cout << where << " page " << narrow(id) << ": width " << sidebarPage->width() << ", content minimum "
						  << (scroll && scroll->widget() ? scroll->widget()->minimumSizeHint().width() : 0) << '\n';
				for (const auto &entry : clipped)
					std::cout << "  clipped: " << narrow(entry) << '\n';
			}
			expect(clipped.isEmpty(), where + ": the " + narrow(id) + " page cuts no control short" +
										  (clipped.isEmpty() ? "" : ": " + narrow(clipped.first())));
			expect(!scroll || !scroll->horizontalScrollBar()->isVisible(), where + ": the " + narrow(id) + " page does not scroll sideways");
		}
	}
	// The profile's timeline flag shows and hides the animation transport.
	auto *timeline = editor->findChild<QToolBar *>(QStringLiteral("meshTimeline"));
	if (expect(timeline, where + ": the animation transport is the meshTimeline tool bar"))
	{
		auto controls = editor->controls();
		controls.layout.timelineVisible = false;
		editor->applyControls(controls);
		app.processEvents();
		expect(!timeline->isVisible(), where + ": a profile without the timeline hides it");
		controls.layout.timelineVisible = true;
		editor->applyControls(controls);
		app.processEvents();
		expect(timeline->isVisible(), where + ": the timeline returns with the profile flag");
	}
	// A status line cut short keeps its whole text, and can be reached with
	// Tab to read it; a whole line is no Tab stop.
	auto *status = dynamic_cast<StatusLineLabel *>(editor->findChild<QLabel *>(QStringLiteral("meshStatus")));
	if (expect(status, where + ": the status line is a StatusLineLabel"))
	{
		const QString message = QStringLiteral("Refused because ") + QStringLiteral("the selection is long ").repeated(40).trimmed();
		status->setText(message);
		app.processEvents();
		expect(status->isElided() && status->text() == message && status->focusPolicy() == Qt::TabFocus,
			   where + ": a status line cut short keeps its text and takes keyboard focus");
		status->setText(QStringLiteral("Ready."));
		app.processEvents();
		expect(!status->isElided() && status->focusPolicy() == Qt::NoFocus, where + ": a whole status line takes no keyboard focus");
		expect(status->height() <= status->fontMetrics().lineSpacing() * 2, where + ": the status line stays one line high");
	}
	// Closing can ask whether to save; destroying the editor skips the question.
	delete editor;
}

// Each dialog, opened on a 1280x720 screen, fits it with its buttons inside.
void checkDialog(QApplication &app, const char *name, QDialog *dialog, const QRect &screen)
{
	std::unique_ptr<QDialog> owner(dialog);
	dialog->show();
	app.processEvents();
	app.processEvents();
	const QSize size = dialog->size(), minimum = dialog->minimumSizeHint();
	const QRect room = screen.adjusted(dialogScreenMargin, dialogScreenMargin, -dialogScreenMargin, -dialogScreenMargin);
	if (reporting())
	{
		std::cout << "dialog " << name << ": " << size.width() << "x" << size.height() << ", minimum " << minimum.width() << "x"
				  << minimum.height() << '\n';
		if (minimum.width() > room.width() || minimum.height() > room.height())
			reportMinimums(dialog, room.size());
	}
	expect(size.width() <= room.width() && size.height() <= room.height(),
		   std::string(name) + ": opens within the screen (" + std::to_string(size.width()) + "x" + std::to_string(size.height()) + ")");
	expect(minimum.width() <= room.width() && minimum.height() <= room.height(),
		   std::string(name) + ": can shrink to the screen (minimum " + std::to_string(minimum.width()) + "x" + std::to_string(minimum.height()) +
			   ")");
	const auto boxes = dialog->findChildren<QDialogButtonBox *>();
	bool shown = false;
	for (auto *box : boxes)
	{
		if (!box->isVisible())
			continue;
		shown = true;
		const QRect area(box->mapTo(dialog, QPoint(0, 0)), box->size());
		expect(dialog->rect().contains(area), std::string(name) + ": its buttons lie inside the dialog");
	}
	expect(shown || boxes.isEmpty(), std::string(name) + ": its buttons are shown");
	const auto clipped = clippedControls(dialog);
	if (reporting())
	{
		for (const auto &entry : clipped)
			std::cout << "  clipped: " << narrow(entry) << '\n';
	}
	expect(clipped.isEmpty(), std::string(name) + ": no shown control is cut short" + (clipped.isEmpty() ? "" : ": " + narrow(clipped.first())));
	// A scrolling body scrolls down, never sideways, while the screen has the
	// width its content needs.
	if (auto *body = dialog->findChild<QScrollArea *>(QStringLiteral("dialogBodyScroll")))
	{
		if (reporting() && body->widget())
		{
			std::cout << "  body content minimum " << body->widget()->minimumSizeHint().width() << " in " << body->viewport()->width() << '\n';
			if (body->horizontalScrollBar()->isVisible())
				reportMinimums(body->widget(), body->viewport()->size());
		}
		expect(!body->horizontalScrollBar()->isVisible(), std::string(name) + ": its content is not wider than the dialog");
	}
	expect(capture(*dialog, QStringLiteral("dialog-%1").arg(QString::fromLatin1(name))), std::string(name) + ": save the render");
}

void checkDialogs(QApplication &app, const QDir &directory)
{
	const QRect screen(0, 0, 1280, 720);
	setDialogScreenAreaOverride(screen);
	ModelDesign design;
	design.parts << ModelDesignPart{};
	const auto plane = buildModelDesignMesh(design);
	const auto rig = tests::ikFixture();
	const auto curves = tests::curveKeysFixture();
	const ModelSelection whole{0, {}, {0}};
	checkDialog(app, "prop-designer", new ModelDesignDialog, screen);
	checkDialog(app, "assembly", new ModelAssemblyDialog, screen);
	checkDialog(app, "controls", new ModelControlsDialog(modelEditorControlsForProfile(QStringLiteral("studio"))), screen);
	checkDialog(app, "surfaces", new ModelSurfaceDialog(plane, whole), screen);
	checkDialog(app, "material-slots", new ModelMaterialSlotsDialog(plane, whole), screen);
	checkDialog(app, "ik", new ModelIkDialog(rig, QStringLiteral("hand"), 0), screen);
	checkDialog(app, "curves", new ModelAnimationCurvesDialog(curves, QStringLiteral("upper"), 0), screen);
	checkDialog(app, "curve-keys", new ModelCurveKeysDialog(curves, QStringLiteral("upper"), 0), screen);
	checkDialog(app, "curve-transfer", new ModelCurveTransferDialog(curves, QStringLiteral("upper"), 0), screen);
	checkDialog(app, "weight-paint", new ModelWeightPaintDialog(rig, whole, QStringLiteral("upper"), 0), screen);
	checkDialog(app, "skinning", new ModelSkinningDialog(rig, whole, QStringLiteral("upper"), 0), screen);
	checkDialog(app, "weight-transfer", new ModelWeightTransferDialog(tests::transferFixture(), whole, 0), screen);
	checkDialog(app, "rig", new ModelRigDialog(rig, whole, QStringLiteral("upper")), screen);
	checkDialog(app, "skeletal", new ModelSkeletalDialog(rig, whole, QStringLiteral("upper"), 0), screen);
	checkDialog(app, "md5-export", new ModelMd5ExportDialog(rig, 0, false), screen);
	QImage texture(64, 64, QImage::Format_RGB32);
	texture.fill(Qt::gray);
	checkDialog(app, "texel-density",
				new ModelUvDensityDialog(tests::densityFixture(), whole, 0, texture, texture.size(), QStringLiteral("models/box.tga")), screen);
	const auto source = directory.filePath(QStringLiteral("damaged.mesh.json"));
	ModelImportRepairPlan plan;
	QString error;
	if (expect(tests::writeImportRepairFixture(source, tests::importRepairSource(tests::damagedImportFixture())) &&
				   prepareModelImportRepair(source, &plan, &error),
			   "prepare an import repair plan"))
		checkDialog(app, "import-repair", new ModelImportRepairDialog(plan, true), screen);
	checkDialog(app, "skin-source", new ModelSkinSourceDialog({}, 0, 0, 0.1), screen);
	// A two-part Quake III player for the animation.cfg editor.
	const auto part = directory.filePath(QStringLiteral("part.mesh.json"));
	const auto assembly = tests::q3Assembly(part);
	ModelAssemblyResolved resolved;
	if (expect(tests::q3Write(part, QJsonDocument(editableModelJson(tests::q3Model())).toJson()) &&
				   resolveModelAssembly(assembly, {directory.path(), {}, {}}, &resolved, &error),
			   "resolve a Quake III player assembly"))
		checkDialog(app, "q3-animation", new ModelQ3AnimationDialog(assembly, resolved), screen);
	setDialogScreenAreaOverride({});
}

// A wrapping check box toggles from its indicator and its caption, the last
// line of a wrapped caption included, and not from the blank rest of a row a
// layout stretched it across, left to right and right to left.
void checkCheckBoxHits(QApplication &app)
{
	for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft})
	{
		const std::string where = direction == Qt::LeftToRight ? "check box" : "check box, right to left";
		QWidget host;
		host.setLayoutDirection(direction);
		auto *layout = new QVBoxLayout(&host);
		auto *box = new WrappingCheckBox(QStringLiteral("Replace existing entry"));
		layout->addWidget(box);
		host.resize(640, 120);
		host.show();
		app.processEvents();
		const auto click = [&](const QPoint &at)
		{
			QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), box->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::LeftButton,
							  Qt::NoModifier);
			QApplication::sendEvent(box, &press);
			QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), box->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::NoButton,
								Qt::NoModifier);
			QApplication::sendEvent(box, &release);
			app.processEvents();
		};
		const bool leftToRight = direction == Qt::LeftToRight;
		// Where the caption begins, just past the indicator.
		const auto captionStart = [&]
		{
			QStyleOptionButton option;
			option.initFrom(box);
			option.rect = box->rect();
			const QRect indicator = box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box);
			return leftToRight ? indicator.right() + 12 : indicator.left() - 12;
		};
		click(QPoint(leftToRight ? box->width() - 12 : 12, box->height() / 2));
		expect(box->width() > 400 && !box->isChecked(), where + ": a click on the blank rest of its row changes nothing");
		click(QPoint(captionStart(), box->height() / 2));
		expect(box->isChecked(), where + ": a click on its caption toggles it");
		// Narrow, the caption wraps; its last line toggles the box too.
		box->setText(QStringLiteral("Refuse exports that leave data out of the written file"));
		host.resize(box->minimumSizeHint().width() + 24, 200);
		app.processEvents();
		const int line = box->fontMetrics().lineSpacing();
		click(QPoint(captionStart(), box->height() - line / 2 - 2));
		expect(box->height() >= 2 * line && !box->isChecked(), where + ": a click on the last line of a wrapped caption toggles it (" +
																	 std::to_string(box->height()) + " px high)");
	}
}

void checkBounds()
{
	// Room on a laptop, a scaled 1080p screen and a 4K one; a minimum wins.
	const QSize preferred(1180, 800), minimum(600, 400);
	expect(boundedDialogSize(preferred, minimum, {1366, 768}) == QSize(1180, 728), "a laptop keeps the width and loses height");
	expect(boundedDialogSize(preferred, minimum, {1280, 720}) == QSize(1180, 680), "1080p at 150% fits in 1280x720");
	expect(boundedDialogSize(preferred, minimum, {3840, 2160}) == preferred, "a 4K screen gives the preferred size");
	expect(boundedDialogSize(preferred, minimum, {500, 300}) == minimum, "never below the minimum");
	expect(boundedDialogSize({2000, 1600}, {}, {1280, 720}) == QSize(1240, 680), "both edges bounded");
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = test_support::exitCodeWithoutRenderer("model-editor-layout-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-layout-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const QDir directory(temporary.path());
	StudioSettings::setOverrideFilePath(directory.filePath(QStringLiteral("settings.ini")));
	checkBounds();
	checkCheckBoxHits(app);
	// High-contrast light with longer strings, right to left; high-contrast
	// dark in English, left to right.
	const Variant variants[]{{"dark-100", StudioTheme::Dark, 100, false, false},
							 {"contrast-light-200-rtl", StudioTheme::HighContrastLight, 200, true, true},
							 {"contrast-dark-200", StudioTheme::HighContrastDark, 200, false, false}};
	for (const auto &variant : variants)
	{
		Expansion expansion;
		if (variant.expanded)
			app.installTranslator(&expansion);
		app.setLayoutDirection(variant.rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(variant.theme, UiDensity::Standard, variant.scale));
		for (const QSize size : {QSize(1280, 720), QSize(1366, 768), QSize(1600, 1000)})
			checkEditor(app, variant, size);
		if (variant.scale == 200)
			checkDialogs(app, directory);
		if (variant.expanded)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	if (failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model editor layout UI smoke passed.\n";
	return EXIT_SUCCESS;
}
