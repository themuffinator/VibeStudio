// The Mesh Editor's layout and controls profiles: two tabbed sidebars in the
// manner of the Levels page and Blender's Properties editor, arranged by the
// profile's family (studio, Blender, 3ds Max, MilkShape 3D); one view or four
// panes; and the profile picker with its reference, customisation, export
// and import.
#include "app/model_editor_dialog.h"

#include "app/dialog_screen_fit.h"
#include "app/form_row_label_lines.h"
#include "app/model_controls_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/status_line_label.h"
#include "app/studio_accessibility.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "app/wrapping_action_button.h"
#include "app/wrapping_check_box.h"
#include "core/model_sidebar.h"
#include "core/studio_settings.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QShowEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <utility>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelEditor)
};

// The smallest a view pane gets, smaller than a lone viewport's own minimum.
constexpr QSize paneMinimum(160, 120);

QString paneObjectName(int quadrant)
{
	return QStringLiteral("meshPane%1").arg(quadrant);
}

// Orbit angles that look at the model from each side, as the View menu does.
void paneAngles(ModelPaneView view, double *yaw, double *pitch)
{
	switch (view)
	{
	case ModelPaneView::Top:
		*yaw = -90;
		*pitch = 90;
		return;
	case ModelPaneView::Bottom:
		*yaw = -90;
		*pitch = -90;
		return;
	case ModelPaneView::Front:
		*yaw = -90;
		*pitch = 0;
		return;
	case ModelPaneView::Back:
		*yaw = 90;
		*pitch = 0;
		return;
	case ModelPaneView::Left:
		*yaw = 180;
		*pitch = 0;
		return;
	case ModelPaneView::Right:
		*yaw = 0;
		*pitch = 0;
		return;
	case ModelPaneView::Perspective:
		break;
	}
	*yaw = 30;
	*pitch = 20;
}
} // namespace

// --- Sidebars --------------------------------------------------------------

SidebarPage *ModelEditorDialog::createSidebarPage(const QString &pageId)
{
	if (auto *existing = m_sidebarPages.value(pageId))
		return existing;
	ModelSidebarTab tab;
	static_cast<void>(modelSidebarTabForId(pageId, &tab));
	auto *page = new SidebarPage(pageId, tab.iconName, tab.title, tab.description);
	page->setObjectName(QStringLiteral("meshPage-") + pageId);
	m_sidebarPages.insert(pageId, page);
	return page;
}

QFormLayout *ModelEditorDialog::addSidebarForm(const QString &pageId, StudioSidebar *sidebar)
{
	auto *page = createSidebarPage(pageId);
	auto *contents = new QWidget;
	contents->setObjectName(QStringLiteral("meshPageForm-") + pageId);
	auto *form = new QFormLayout(contents);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	page->addWidget(contents);
	sidebar->addPage(page);
	return form;
}

void ModelEditorDialog::buildSidebarPages()
{
	// The pages are created where their controls are built; this only makes
	// sure every tab the arrangement names exists.
	for (const auto &id : modelSidebarTabIds())
		createSidebarPage(id);
}

void ModelEditorDialog::buildToolPages()
{
	buildSidebarPages();
	// Add: the Add menu's primitives as buttons.
	if (auto *add = m_sidebarPages.value(QStringLiteral("add")))
	{
		auto *body = new QWidget;
		body->setObjectName(QStringLiteral("meshAddPrimitives"));
		auto *grid = new QGridLayout(body);
		grid->setContentsMargins(0, 0, 0, 0);
		int index = 0;
		if (auto *menu = m_tools->menuBar()->findChild<QMenu *>(QStringLiteral("meshMenuAdd")))
		{
			for (auto *action : menu->actions())
			{
				if (action->isSeparator() || action->menu())
					continue;
				auto *button = new QToolButton;
				button->setDefaultAction(action);
				button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
				button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
				// One column, as Blender's Add menu lists them: two columns of
				// captions would widen the sidebar at large text sizes.
				grid->addWidget(button, index, 0);
				++index;
			}
		}
		if (index == 0)
		{
			auto *hint = new QLabel(Text::tr("Shift+A in a view opens the Add menu."));
			hint->setWordWrap(true);
			grid->addWidget(hint, 0, 0);
		}
		add->addSection(QStringLiteral("primitives"), Text::tr("Primitives"), body);
	}
	// View: layout, panes, profile and controls.
	if (auto *view = m_sidebarPages.value(QStringLiteral("view")))
	{
		auto *layoutBody = new QWidget;
		auto *layoutForm = new QFormLayout(layoutBody);
		layoutForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
		m_layoutCombo = new QComboBox;
		m_layoutCombo->setObjectName(QStringLiteral("meshViewLayout"));
		m_layoutCombo->setAccessibleName(Text::tr("View layout"));
		m_layoutCombo->addItem(Text::tr("One view"), modelViewLayoutId(ModelViewLayout::Single));
		m_layoutCombo->addItem(Text::tr("Four views"), modelViewLayoutId(ModelViewLayout::FourViews));
		layoutForm->addRow(Text::tr("Layout"), m_layoutCombo);
		connect(m_layoutCombo, &QComboBox::currentIndexChanged, this,
				[this](int index)
				{
					if (!m_refreshing)
						setViewLayout(index == 1 ? ModelViewLayout::FourViews : ModelViewLayout::Single);
				});
		const QString corners[4]{Text::tr("Top left"), Text::tr("Top right"), Text::tr("Bottom left"), Text::tr("Bottom right")};
		for (int quadrant = 0; quadrant < 4; ++quadrant)
		{
			auto *combo = new QComboBox;
			combo->setObjectName(QStringLiteral("meshPaneView%1").arg(quadrant));
			combo->setAccessibleName(Text::tr("%1 view").arg(corners[quadrant]));
			for (ModelPaneView pane : {ModelPaneView::Perspective, ModelPaneView::Top, ModelPaneView::Bottom, ModelPaneView::Front,
									   ModelPaneView::Back, ModelPaneView::Left, ModelPaneView::Right})
				combo->addItem(modelPaneViewDisplayName(pane), modelPaneViewId(pane));
			m_paneCombos[quadrant] = combo;
			layoutForm->addRow(corners[quadrant], combo);
			connect(combo, &QComboBox::currentIndexChanged, this,
					[this, quadrant, combo]
					{
						if (m_refreshing)
							return;
						ModelPaneView pane = ModelPaneView::Perspective;
						modelPaneViewForId(combo->currentData().toString(), &pane);
						if (m_paneViews.size() == 4)
						{
							m_paneViews[quadrant] = pane;
							const auto all = panes();
							if (quadrant < all.size())
								configurePane(all[quadrant], pane, true);
						}
					});
		}
		view->addSection(QStringLiteral("layout"), Text::tr("Views"), layoutBody);

		auto *controlsBody = new QWidget;
		auto *controlsForm = new QFormLayout(controlsBody);
		controlsForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
		m_profileCombo = new QComboBox;
		m_profileCombo->setObjectName(QStringLiteral("meshControlsProfile"));
		m_profileCombo->setAccessibleName(Text::tr("Controls like"));
		for (const auto &profile : modelEditorProfiles())
		{
			m_profileCombo->addItem(profile.displayName, profile.id);
			// The tooltip says what the profile changes and what it leaves out.
			const QString tip = profile.adaptations.isEmpty()
									? profile.description
									: profile.description + QStringLiteral("\n\n") + profile.adaptations.join(QLatin1Char(' '));
			m_profileCombo->setItemData(m_profileCombo->count() - 1, tip, Qt::ToolTipRole);
		}
		controlsForm->addRow(Text::tr("Controls like"), m_profileCombo);
		connect(m_profileCombo, &QComboBox::currentIndexChanged, this,
				[this]
				{
					if (!m_refreshing)
						applyProfile(m_profileCombo->currentData().toString());
				});
		auto *reference = new WrappingActionButton(Text::tr("Show Every Gesture and Key…"), 2);
		reference->setObjectName(QStringLiteral("meshControlsReference"));
		connect(reference, &QPushButton::clicked, this, [this] { showControlsReference(); });
		controlsForm->addRow(reference);
		auto *customise = new WrappingActionButton(Text::tr("Customise Controls…"), 2);
		customise->setObjectName(QStringLiteral("meshCustomiseControls"));
		connect(customise, &QPushButton::clicked, this, [this] { customiseControls(); });
		controlsForm->addRow(customise);
		auto *exportButton = new WrappingActionButton(Text::tr("Export Controls…"), 2);
		exportButton->setObjectName(QStringLiteral("meshExportControls"));
		exportButton->setToolTip(Text::tr("Save your changes to this profile as a file to share or keep."));
		connect(exportButton, &QPushButton::clicked, this, [this] { exportControls(); });
		auto *importButton = new WrappingActionButton(Text::tr("Import Controls…"), 2);
		importButton->setObjectName(QStringLiteral("meshImportControls"));
		importButton->setToolTip(Text::tr("Load a controls file and save its changes for the profile it names."));
		connect(importButton, &QPushButton::clicked, this, [this] { importControls(); });
		controlsForm->addRow(exportButton);
		controlsForm->addRow(importButton);
		view->addSection(QStringLiteral("controls"), Text::tr("Controls"), controlsBody);

		auto *overlays = new QWidget;
		auto *overlayRows = new QVBoxLayout(overlays);
		overlayRows->setContentsMargins(0, 0, 0, 0);
		const auto toggle = [&](const QString &name, const QString &text, bool checked, auto apply)
		{
			auto *box = new WrappingCheckBox(text);
			box->setObjectName(name);
			box->setChecked(checked);
			connect(box, &QCheckBox::toggled, this, apply);
			overlayRows->addWidget(box);
		};
		toggle(QStringLiteral("meshShowGrid"), Text::tr("Grid"), m_preview->showGrid(),
			   [this](bool on)
			   {
				   m_preview->setShowGrid(on);
				   syncPanes();
			   });
		toggle(QStringLiteral("meshShowAxes"), Text::tr("Axes"), m_preview->showAxes(),
			   [this](bool on)
			   {
				   m_preview->setShowAxes(on);
				   syncPanes();
			   });
		toggle(QStringLiteral("meshShowEdges"), Text::tr("Edged faces"), m_preview->showEdges(),
			   [this](bool on)
			   {
				   m_preview->setShowEdges(on);
				   syncPanes();
			   });
		toggle(QStringLiteral("meshBackfaceCulling"), Text::tr("Hide back faces"), m_preview->backfaceCulling(),
			   [this](bool on)
			   {
				   m_preview->setBackfaceCulling(on);
				   syncPanes();
			   });
		view->addSection(QStringLiteral("overlays"), Text::tr("Overlays"), overlays);
	}
}

void ModelEditorDialog::arrangeSidebars()
{
	const QString family = m_controls.layout.family;
	ModelSidebarArrangement arrangement = modelSidebarArrangementForFamily(family);
	const QJsonObject state = StudioSettings(StudioSettings::AccessMode::ReadOnly).modelSidebarState(family);
	ModelSidebarArrangement saved;
	if (state.contains(QStringLiteral("leading")) && modelSidebarArrangementFromJson(state, &saved))
		arrangement = saved;
	arrangement = normalizedModelSidebarArrangement(arrangement, family);
	if (!state.value(QStringLiteral("leadingCurrent")).toString().isEmpty() && arrangement.leading.contains(state.value(QStringLiteral("leadingCurrent")).toString()))
		arrangement.leadingCurrent = state.value(QStringLiteral("leadingCurrent")).toString();
	if (!state.value(QStringLiteral("trailingCurrent")).toString().isEmpty() &&
		arrangement.trailing.contains(state.value(QStringLiteral("trailingCurrent")).toString()))
		arrangement.trailingCurrent = state.value(QStringLiteral("trailingCurrent")).toString();
	const QSignalBlocker leadingBlocker(m_leadingSidebar), trailingBlocker(m_trailingSidebar);
	for (auto *sidebar : {m_leadingSidebar, m_trailingSidebar})
	{
		for (const auto &id : sidebar->pageIds())
			sidebar->takePage(id);
	}
	const auto place = [&](StudioSidebar *sidebar, const QStringList &ids)
	{
		for (const auto &id : ids)
		{
			auto *page = m_sidebarPages.value(id);
			if (!page)
				continue;
			page->setTitle(modelSidebarTabTitle(id, family));
			sidebar->addPage(page, arrangement.groupStarts.contains(id));
			page->show();
		}
	};
	place(m_leadingSidebar, arrangement.leading);
	place(m_trailingSidebar, arrangement.trailing);
	if (!arrangement.leadingCurrent.isEmpty())
		m_leadingSidebar->showPage(arrangement.leadingCurrent);
	if (!arrangement.trailingCurrent.isEmpty())
		m_trailingSidebar->showPage(arrangement.trailingCurrent);
	m_leadingSidebar->setVisible(!arrangement.leading.isEmpty() && !m_sidebarsHidden);
	m_trailingSidebar->setVisible(!arrangement.trailing.isEmpty() && !m_sidebarsHidden);
	// Families name selection modes their own way (Polygon, Group, Joint).
	const QStringList modes = modelSelectionModeNames(family);
	for (int index = 0; index < modes.size() && index < m_selectionMode->count(); ++index)
		m_selectionMode->setItemText(index, modes[index]);
}

void ModelEditorDialog::fitSidebarPages()
{
	for (auto *page : std::as_const(m_sidebarPages))
	{
		auto *scroll = page->findChild<QScrollArea *>(QStringLiteral("sidebarPageScroll"));
		if (!scroll || !scroll->widget())
			continue;
		auto *content = scroll->widget();
		// Text gains lines rather than width: labels, summaries and notes
		// wrap, and a choice shows ten characters at least (its list shows
		// each whole). Buttons and check boxes are the wrapping kinds.
		for (auto *form : content->findChildren<QFormLayout *>())
		{
			// A form that keeps labels beside their fields moves a field under
			// its label when the two do not fit side by side.
			if (form->rowWrapPolicy() == QFormLayout::DontWrapRows)
				form->setRowWrapPolicy(QFormLayout::WrapLongRows);
			for (int row = 0; row < form->rowCount(); ++row)
			{
				// A row label's wrapped lines need their height kept.
				if (auto *item = form->itemAt(row, QFormLayout::LabelRole); item)
					keepFormRowLabelLines(qobject_cast<QLabel *>(item->widget()));
			}
		}
		for (auto *label : content->findChildren<QLabel *>())
		{
			if (!dynamic_cast<ElidedLabel *>(label) && !dynamic_cast<StatusLineLabel *>(label))
				label->setWordWrap(true);
		}
		for (auto *choice : content->findChildren<QComboBox *>())
		{
			if (choice->sizeAdjustPolicy() != QComboBox::AdjustToMinimumContentsLengthWithIcon)
			{
				choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
				choice->setMinimumContentsLength(10);
			}
		}
		// The page is as wide as what is left that cannot wrap (a number
		// field, a long word), and at least 18 average characters (16 for the
		// outliner), so the floor follows the text scale. The editor folds a
		// sidebar when both no longer leave the views room.
		page->ensurePolished();
		const int borders = 2 * page->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, page);
		const int floor = page->fontMetrics().averageCharWidth() * (page->pageId() == QStringLiteral("outliner") ? 16 : 18);
		const int width = std::max(content->minimumSizeHint().width(), floor);
		page->setMinimumWidth(width + scroll->verticalScrollBar()->sizeHint().width() + borders);
	}
}

void ModelEditorDialog::queueViewsFit(StudioSidebar *opened)
{
	if (!m_tools)
		return;
	if (opened)
		m_viewsFitOpened = opened;
	if (m_viewsFitQueued)
		return;
	m_viewsFitQueued = true;
	QTimer::singleShot(0, this, [this] { fitViewsToWidth(); });
}

void ModelEditorDialog::foldSidebarForViews(StudioSidebar *sidebar, const QString &message)
{
	auto *splitter = qobject_cast<QSplitter *>(m_editingControls);
	auto *other = sidebar == m_leadingSidebar ? m_trailingSidebar : m_leadingSidebar;
	const int otherIndex = splitter && other ? splitter->indexOf(other) : -1;
	const int before = otherIndex >= 0 ? splitter->sizes().value(otherIndex) : 0;
	sidebar->setFolded(true);
	// Folding hands the width to the widest neighbour, which can be the other
	// sidebar (just opened, say): it goes to the views instead.
	if (otherIndex >= 0)
	{
		QList<int> sizes = splitter->sizes();
		if (sizes.size() == 3 && otherIndex != 1 && sizes[otherIndex] > before)
		{
			sizes[1] += sizes[otherIndex] - before;
			sizes[otherIndex] = before;
			splitter->setSizes(sizes);
		}
	}
	m_status->setText(message, LiveStatus::Result);
}

void ModelEditorDialog::fitViewsToWidth()
{
	m_viewsFitQueued = false;
	auto *opened = std::exchange(m_viewsFitOpened, nullptr);
	auto *splitter = qobject_cast<QSplitter *>(m_editingControls);
	if (!splitter || !m_paneGrid || !m_leadingSidebar || !m_trailingSidebar || !isVisible() || width() <= 0)
		return;
	QList<int> sizes = splitter->sizes();
	if (sizes.size() != 3)
		return;
	// The shelf and margins beside the views stay; the views get the rest of
	// the centre column.
	const int beside = std::max(0, sizes[1] - m_paneGrid->width());
	const int wanted = (width() * 45 + 99) / 100;
	if (sizes[1] - beside >= wanted)
		return;
	// The sidebars first give back what they hold beyond their minimum.
	for (const int index : {2, 0})
	{
		auto *sidebar = index == 0 ? m_leadingSidebar : m_trailingSidebar;
		if (!sidebar->isVisible() || sidebar->isFolded())
			continue;
		const int give = std::min(sizes[index] - sidebar->minimumSizeHint().width(), wanted - (sizes[1] - beside));
		if (give > 0)
		{
			sizes[index] -= give;
			sizes[1] += give;
		}
	}
	splitter->setSizes(sizes);
	// Then a sidebar folds to its tabs, as Blender's N folds the properties
	// sidebar: that one, unless the user has just opened it, when the other
	// folds instead. Any of its tabs opens it again.
	auto *fold = opened == m_trailingSidebar ? m_leadingSidebar : m_trailingSidebar;
	if (sizes[1] - beside >= wanted || !fold->isVisible() || fold->isFolded() || fold->pageIds().isEmpty())
		return;
	foldSidebarForViews(fold, opened && opened != fold
								  ? Text::tr("The %1 sidebar is folded to make room for %2. Choose one of its tabs to open it.")
										.arg(fold->accessibleName(), opened->accessibleName())
								  : Text::tr("The %1 sidebar is folded to give the views room. Choose one of its tabs to open it.")
										.arg(fold->accessibleName()));
}

void ModelEditorDialog::keepSidebarsInRoom(StudioSidebar *opened)
{
	auto *other = opened == m_leadingSidebar ? m_trailingSidebar : m_leadingSidebar;
	if (!opened || !other || other->isFolded() || !other->isVisible() || other->pageIds().isEmpty())
		return;
	// The room the editor has: its page in the studio, else the screen. The
	// panels' minimum is read from the splitter, which measures it afresh;
	// the editor's own layout catches up only after the next layout pass.
	const int room = isWindow() ? dialogScreenArea(this).width() : (parentWidget() ? parentWidget()->width() : 0);
	const QMargins margins = layout() ? layout()->contentsMargins() : QMargins();
	if (room <= 0 || !m_editingControls || m_editingControls->minimumSizeHint().width() + margins.left() + margins.right() <= room)
		return;
	// Both sidebars no longer fit beside the views: the one just opened stays
	// and the other folds to its tabs, rather than the editor (menus and all)
	// scrolling sideways in its page.
	foldSidebarForViews(other, Text::tr("The %1 sidebar is folded to make room for %2. Choose one of its tabs to open it.")
								   .arg(other->accessibleName(), opened->accessibleName()));
}

void ModelEditorDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	// A new text scale or theme changes every control's width: refit the
	// sidebar floors, then make room for the views again.
	if ((event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) && m_tools)
	{
		fitSidebarPages();
		queueViewsFit();
	}
}

void ModelEditorDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	queueViewsFit();
}

void ModelEditorDialog::resizeEvent(QResizeEvent *event)
{
	QDialog::resizeEvent(event);
	// Only narrowing can crowd the views; widening leaves the user's sizes.
	if (event->size().width() < event->oldSize().width())
		queueViewsFit();
}

void ModelEditorDialog::saveSidebarState()
{
	if (!m_leadingSidebar || !m_trailingSidebar)
		return;
	ModelSidebarArrangement arrangement;
	arrangement.leading = m_leadingSidebar->pageIds();
	arrangement.trailing = m_trailingSidebar->pageIds();
	arrangement.groupStarts = modelSidebarArrangementForFamily(m_controls.layout.family).groupStarts;
	arrangement.leadingCurrent = m_leadingSidebar->currentPageId();
	arrangement.trailingCurrent = m_trailingSidebar->currentPageId();
	StudioSettings settings;
	if (settings.isReadOnly())
		return;
	settings.setModelSidebarState(m_controls.layout.family, modelSidebarArrangementJson(arrangement, m_controls.layout.family));
}

bool ModelEditorDialog::showSidebarPage(const QString &pageId)
{
	for (auto *sidebar : {m_trailingSidebar, m_leadingSidebar})
	{
		if (sidebar && sidebar->showPage(pageId))
		{
			sidebar->show();
			return true;
		}
	}
	return false;
}

void ModelEditorDialog::toggleSidebars()
{
	// Blender's N: the properties sidebar beside the view folds and opens.
	if (m_trailingSidebar)
		m_trailingSidebar->setFolded(!m_trailingSidebar->isFolded());
}

// --- Profiles --------------------------------------------------------------

void ModelEditorDialog::applyProfile(const QString &profileId, bool save)
{
	const QString id = normalizedModelEditorProfileId(profileId).isEmpty() ? defaultModelEditorProfileId() : normalizedModelEditorProfileId(profileId);
	if (save)
	{
		StudioSettings settings;
		if (!settings.isReadOnly())
			settings.setModelEditorProfileId(id);
	}
	QStringList warnings;
	applyControls(StudioSettings(StudioSettings::AccessMode::ReadOnly).effectiveModelEditorControls(id, &warnings));
	if (!warnings.isEmpty())
		m_status->setText(warnings.join(QLatin1Char(' ')));
	if (save && profileChanged)
		profileChanged();
}

void ModelEditorDialog::applyControls(const ModelEditorControls &controls)
{
	const bool familyChanged = controls.layout.family != m_controls.layout.family || m_sidebarPages.value(QStringLiteral("outliner"))->parentWidget() == nullptr;
	const bool layoutChanged = controls.layout.layout != m_controls.layout.layout || controls.layout.panes != m_controls.layout.panes || m_paneViews.isEmpty();
	m_controls = controls;
	if (m_paneViews.size() != 4 || layoutChanged)
	{
		m_paneViews = m_controls.layout.panes;
		if (m_paneViews.size() != 4)
			m_paneViews = {ModelPaneView::Top, ModelPaneView::Front, ModelPaneView::Right, ModelPaneView::Perspective};
		// The interactive view starts in the profile's 3D pane.
		m_activePane = std::max(0, int(m_paneViews.indexOf(ModelPaneView::Perspective)));
		if (m_paneViews.indexOf(ModelPaneView::Perspective) < 0)
			m_activePane = 3;
	}
	if (m_tools)
		m_tools->applyControls(m_controls);
	arrangeSidebars();
	Q_UNUSED(familyChanged);
	if (m_tools && m_tools->action(QStringLiteral("meshToggleToolShelf")))
	{
		if (auto *shelf = findChild<QToolBar *>(QStringLiteral("meshToolShelf")))
			shelf->setVisible(m_controls.layout.toolShelfVisible);
	}
	if (auto *timeline = findChild<QToolBar *>(QStringLiteral("meshTimeline")))
		timeline->setVisible(m_controls.layout.timelineVisible);
	if (layoutChanged)
	{
		// The opposite layout first, so the panes are placed whichever layout
		// was showing (forcing one view left four panes on screen).
		m_viewLayout = m_controls.layout.layout == ModelViewLayout::Single ? ModelViewLayout::FourViews : ModelViewLayout::Single;
		setViewLayout(m_controls.layout.layout);
		if (m_controls.layout.layout == ModelViewLayout::Single)
		{
			const QSignalBlocker blocker(m_viewPreset);
			m_viewPreset->setCurrentIndex(m_controls.layout.startInPerspective ? 4 : 0);
			configurePane(m_preview, m_controls.layout.startInPerspective ? ModelPaneView::Perspective : ModelPaneView::Perspective, false);
			if (!m_controls.layout.startInPerspective)
			{
				auto camera = m_preview->cameraControls();
				camera.perspective = false;
				m_preview->setCameraControls(camera);
				m_preview->setOrbit(30, 20);
			}
			m_preview->frameModel();
		}
	}
	else
		configurePane(m_preview, m_viewLayout == ModelViewLayout::FourViews ? m_paneViews.value(m_activePane) : ModelPaneView::Perspective, false);
	refreshProfileControls();
	// Arranging the sidebars opens them again: make room for the views.
	if (isVisible())
		queueViewsFit();
}

void ModelEditorDialog::refreshProfileControls()
{
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(m_controls.profileId, &profile));
	const bool refreshing = m_refreshing;
	m_refreshing = true;
	if (m_profileCombo)
		m_profileCombo->setCurrentIndex(std::max(0, m_profileCombo->findData(m_controls.profileId)));
	if (m_layoutCombo)
		m_layoutCombo->setCurrentIndex(m_viewLayout == ModelViewLayout::FourViews ? 1 : 0);
	for (int quadrant = 0; quadrant < 4; ++quadrant)
	{
		if (m_paneCombos[quadrant])
		{
			m_paneCombos[quadrant]->setCurrentIndex(std::max(0, m_paneCombos[quadrant]->findData(modelPaneViewId(m_paneViews.value(quadrant)))));
			m_paneCombos[quadrant]->setEnabled(m_viewLayout == ModelViewLayout::FourViews);
		}
	}
	m_refreshing = refreshing;
	if (m_profileButton)
	{
		m_profileButton->setText(Text::tr("Controls: %1").arg(profile.shortName));
		m_profileButton->setToolTip(profile.description + QStringLiteral("\n\n") + profile.adaptations.join(QLatin1Char('\n')));
		m_profileButton->setAccessibleDescription(m_profileButton->toolTip());
		if (auto *menu = m_profileButton->menu())
		{
			for (auto *action : menu->actions())
			{
				if (action->data().isValid() && action->isCheckable())
					action->setChecked(action->data().toString() == m_controls.profileId);
			}
		}
	}
}

void ModelEditorDialog::showControlsReference()
{
	QDialog dialog(this);
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(m_controls.profileId, &profile));
	dialog.setObjectName(QStringLiteral("meshControlsReferenceDialog"));
	dialog.setWindowTitle(Text::tr("%1 Controls").arg(profile.displayName));
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(profile.description + QStringLiteral("\n\n") + profile.adaptations.join(QLatin1Char('\n')));
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::PlainText);
	layout->addWidget(intro);
	auto *table = new QTableWidget;
	table->setObjectName(QStringLiteral("meshControlsReferenceTable"));
	table->setAccessibleName(Text::tr("Every gesture and key"));
	table->setColumnCount(3);
	table->setHorizontalHeaderLabels({Text::tr("Where"), Text::tr("Action"), Text::tr("Gesture or key")});
	table->verticalHeader()->hide();
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	// Arrows move between rows; Tab leaves rather than cycling every cell.
	table->setTabKeyNavigation(false);
	const auto rows = modelEditorControlRows(m_controls);
	table->setRowCount(rows.size());
	for (int row = 0; row < rows.size(); ++row)
	{
		table->setItem(row, 0, new QTableWidgetItem(rows[row].view));
		table->setItem(row, 1, new QTableWidgetItem(rows[row].action));
		table->setItem(row, 2, new QTableWidgetItem(rows[row].gesture));
	}
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setStretchLastSection(true);
	layout->addWidget(table, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	fitDialogToScreen(&dialog, QSize(720, 640));
	dialog.exec();
}

void ModelEditorDialog::customiseControls()
{
	ModelControlsDialog dialog(m_controls, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	const ModelEditorControls base = modelEditorControlsForProfile(m_controls.profileId);
	const QJsonObject overrides = vibestudio::modelEditorControlOverrides(base, dialog.controls());
	StudioSettings settings;
	QString error;
	if (!settings.setModelEditorControlOverrides(m_controls.profileId, overrides, &error))
	{
		m_status->setText(error, LiveStatus::Problem);
		return;
	}
	applyControls(dialog.controls());
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(m_controls.profileId, &profile));
	m_status->setText(overrides.isEmpty() ? Text::tr("The %1 controls are back to their defaults.").arg(profile.displayName)
										  : Text::tr("Saved your changes to the %1 controls.").arg(profile.displayName),
					  LiveStatus::Result);
}

void ModelEditorDialog::exportControls()
{
	const QString path = QFileDialog::getSaveFileName(this, Text::tr("Export Controls"), m_controls.profileId + QStringLiteral("-controls.json"),
													  Text::tr("VibeStudio controls (*.json)"));
	if (path.isEmpty())
		return;
	const ModelEditorControls base = modelEditorControlsForProfile(m_controls.profileId);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) ||
		file.write(modelEditorControlsFile(m_controls.profileId, vibestudio::modelEditorControlOverrides(base, m_controls))) < 0 || !file.commit())
	{
		m_status->setText(Text::tr("Could not write %1: %2").arg(path, file.errorString()), LiveStatus::Problem);
		return;
	}
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(m_controls.profileId, &profile));
	m_status->setText(Text::tr("Exported the %1 controls to %2.").arg(profile.displayName, path), LiveStatus::Result);
}

void ModelEditorDialog::importControls()
{
	const QString path = QFileDialog::getOpenFileName(this, Text::tr("Import Controls"), {}, Text::tr("VibeStudio controls (*.json)"));
	if (!path.isEmpty())
		importControlsFile(path);
}

bool ModelEditorDialog::importControlsFile(const QString &path, QString *error)
{
	const auto fail = [this, error](const QString &message)
	{
		m_status->setText(message, LiveStatus::Problem);
		if (error)
			*error = message;
		return false;
	};
	QFile file(path);
	QString profileId, failure;
	QJsonObject overrides;
	if (!file.open(QIODevice::ReadOnly) || !parseModelEditorControlsFile(file.readAll(), &profileId, &overrides, &failure))
		return fail(failure.isEmpty() ? Text::tr("Could not read %1: %2").arg(path, file.errorString()) : failure);
	// The settings keep only what applies, so what the file asks for and no
	// profile can take (a key that always moves focus between panels, an
	// unknown command) is reported now, not dropped in silence.
	QStringList skipped;
	ModelEditorControls applied;
	applyModelEditorControlOverrides(modelEditorControlsForProfile(profileId), overrides, &applied, &skipped);
	StudioSettings settings;
	if (!settings.setModelEditorControlOverrides(profileId, overrides, &failure))
		return fail(failure);
	applyProfile(profileId);
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(profileId, &profile));
	const QString imported = Text::tr("Imported the %1 controls from %2.").arg(profile.displayName, path);
	m_status->setText(skipped.isEmpty() ? imported : imported + QLatin1Char(' ') + skipped.join(QLatin1Char(' ')), LiveStatus::Result);
	if (error)
		error->clear();
	return true;
}

// --- Panes -----------------------------------------------------------------

void ModelEditorDialog::buildPaneGrid(QWidget *viewport)
{
	m_paneGrid = new QWidget;
	m_paneGrid->setObjectName(QStringLiteral("meshViewPanes"));
	m_paneGrid->setAccessibleName(Text::tr("Model views"));
	m_paneLayout = new QGridLayout(m_paneGrid);
	m_paneLayout->setContentsMargins(0, 0, 0, 0);
	m_paneLayout->setSpacing(2);
	for (int quadrant = 0; quadrant < 4; ++quadrant)
	{
		auto *frame = new QFrame;
		frame->setObjectName(paneObjectName(quadrant));
		frame->setProperty("modelPane", true);
		frame->setProperty("activePane", false);
		auto *column = new QVBoxLayout(frame);
		column->setContentsMargins(0, 0, 0, 0);
		column->setSpacing(0);
		m_paneFrames[quadrant] = frame;
		m_paneLayout->addWidget(frame, quadrant / 2, quadrant % 2);
	}
	m_paneFrames[m_activePane]->layout()->addWidget(viewport);
	// Four panes at the viewport's own minimum would hold the editor open on
	// a laptop screen at large text sizes.
	viewport->setMinimumSize(paneMinimum);
	m_paneSync = new QTimer(this);
	m_paneSync->setSingleShot(true);
	m_paneSync->setInterval(30);
	connect(m_paneSync, &QTimer::timeout, this,
			[this]
			{
				for (auto *mirror : std::as_const(m_mirrors))
					mirror->mirrorDisplayFrom(*m_preview);
			});
	connect(static_cast<ModelViewport *>(viewport), &ModelViewport::frameChanged, this, [this] { syncPanes(); });
	connect(static_cast<ModelViewport *>(viewport), &ModelViewport::renderCompleted, this, [this] { syncPanes(); });
	placePanes();
}

QVector<ModelViewport *> ModelEditorDialog::panes() const
{
	QVector<ModelViewport *> result(4, nullptr);
	for (int quadrant = 0; quadrant < 4; ++quadrant)
	{
		if (m_paneFrames[quadrant])
			result[quadrant] = m_paneFrames[quadrant]->findChild<ModelViewport *>(QString(), Qt::FindDirectChildrenOnly);
	}
	return result;
}

void ModelEditorDialog::configurePane(ModelViewport *pane, ModelPaneView view, bool frame)
{
	if (!pane)
		return;
	const bool four = m_viewLayout == ModelViewLayout::FourViews;
	auto camera = (four && view != ModelPaneView::Perspective) ? m_controls.navigation.orthographic : m_controls.navigation.view3D;
	camera.toggleModifiers = Qt::ShiftModifier;
	camera.faceModifiers = Qt::NoModifier;
	camera.perspective = four ? view == ModelPaneView::Perspective : pane->isPerspective();
	pane->setCameraControls(camera, !four);
	if (four && view != ModelPaneView::Perspective)
	{
		double yaw = 0, pitch = 0;
		paneAngles(view, &yaw, &pitch);
		pane->setOrbit(yaw, pitch);
	}
	pane->setAccessibleName(four ? Text::tr("%1 view").arg(modelPaneViewDisplayName(view)) : Text::tr("Editable model preview"));
	// Each of the four panes names its view in the corner, as 3ds Max and
	// MilkShape 3D do.
	pane->setViewLabel(four ? modelPaneViewDisplayName(view) : QString());
	if (frame)
		pane->frameModel();
}

void ModelEditorDialog::placePanes()
{
	if (!m_paneLayout)
		return;
	const bool four = m_viewLayout == ModelViewLayout::FourViews;
	if (four)
	{
		while (m_mirrors.size() < 3)
		{
			auto *mirror = new ModelViewport;
			mirror->setObjectName(QStringLiteral("meshMirrorPane%1").arg(m_mirrors.size()));
			mirror->setMinimumSize(paneMinimum);
			mirror->setBackfaceCulling(false);
			mirror->setFocusPolicy(Qt::ClickFocus);
			mirror->installEventFilter(this);
			connect(mirror, &ModelViewport::playbackRefused, this, [this](const QString &reason) { m_status->setText(reason); });
			m_mirrors.append(mirror);
		}
		int next = 0;
		for (int quadrant = 0; quadrant < 4; ++quadrant)
		{
			if (quadrant == m_activePane)
				continue;
			auto *mirror = m_mirrors[next++];
			if (mirror->parentWidget() != m_paneFrames[quadrant])
				m_paneFrames[quadrant]->layout()->addWidget(mirror);
			mirror->show();
			mirror->mirrorDisplayFrom(*m_preview);
			configurePane(mirror, m_paneViews.value(quadrant), true);
		}
	}
	if (m_preview->parentWidget() != m_paneFrames[m_activePane])
		m_paneFrames[m_activePane]->layout()->addWidget(m_preview);
	for (int quadrant = 0; quadrant < 4; ++quadrant)
	{
		const bool active = quadrant == m_activePane;
		m_paneFrames[quadrant]->setVisible(active || (four && !m_maximised));
		m_paneFrames[quadrant]->setProperty("activePane", four && active);
		m_paneFrames[quadrant]->style()->unpolish(m_paneFrames[quadrant]);
		m_paneFrames[quadrant]->style()->polish(m_paneFrames[quadrant]);
	}
	m_preview->show();
}

void ModelEditorDialog::setViewLayout(ModelViewLayout layout)
{
	if (layout == m_viewLayout && (layout == ModelViewLayout::Single || m_mirrors.size() == 3))
	{
		refreshProfileControls();
		return;
	}
	m_viewLayout = layout;
	m_maximised = false;
	placePanes();
	configurePane(m_preview, layout == ModelViewLayout::FourViews ? m_paneViews.value(m_activePane) : ModelPaneView::Perspective,
				  layout == ModelViewLayout::FourViews);
	refreshProfileControls();
	syncPanes();
}

void ModelEditorDialog::toggleFourViews()
{
	setViewLayout(m_viewLayout == ModelViewLayout::FourViews ? ModelViewLayout::Single : ModelViewLayout::FourViews);
}

void ModelEditorDialog::toggleMaximisedView()
{
	if (m_viewLayout == ModelViewLayout::FourViews)
	{
		// 3ds Max's Alt+W: the active view fills the area, then returns.
		m_maximised = !m_maximised;
		placePanes();
		return;
	}
	// One view: Blender's Ctrl+Space hides the panels around it.
	m_sidebarsHidden = !m_sidebarsHidden;
	m_leadingSidebar->setVisible(!m_sidebarsHidden && !m_leadingSidebar->pageIds().isEmpty());
	m_trailingSidebar->setVisible(!m_sidebarsHidden && !m_trailingSidebar->pageIds().isEmpty());
}

void ModelEditorDialog::activatePane(int quadrant, Qt::FocusReason reason)
{
	if (quadrant < 0 || quadrant > 3 || quadrant == m_activePane || m_viewLayout != ModelViewLayout::FourViews)
		return;
	auto *mirror = m_paneFrames[quadrant]->findChild<ModelViewport *>(QString(), Qt::FindDirectChildrenOnly);
	if (!mirror || mirror == m_preview)
		return;
	// The preview moves into the clicked quadrant and takes its camera; the
	// mirror takes the preview's old place and camera. Both show the same
	// thing, so the swap is seamless and every edit stays on the preview.
	const auto previewState = m_preview->navigationState();
	const auto mirrorState = mirror->navigationState();
	const int previous = m_activePane;
	m_paneFrames[quadrant]->layout()->removeWidget(mirror);
	m_paneFrames[previous]->layout()->removeWidget(m_preview);
	m_paneFrames[quadrant]->layout()->addWidget(m_preview);
	m_paneFrames[previous]->layout()->addWidget(mirror);
	m_activePane = quadrant;
	configurePane(m_preview, m_paneViews.value(quadrant), false);
	configurePane(mirror, m_paneViews.value(previous), false);
	m_preview->restoreNavigationState(mirrorState);
	mirror->restoreNavigationState(previewState);
	mirror->mirrorDisplayFrom(*m_preview);
	placePanes();
	m_paneLayout->activate();
	m_preview->setFocus(reason);
}

bool ModelEditorDialog::stepActivePane(int step)
{
	if (m_viewLayout != ModelViewLayout::FourViews)
	{
		const auto *toggle = m_tools ? m_tools->action(QStringLiteral("meshToggleQuadView")) : nullptr;
		const QString keys = toggle && !toggle->shortcuts().isEmpty() ? toggle->shortcuts().first().toString(QKeySequence::NativeText) : QString();
		m_status->setText(keys.isEmpty() ? Text::tr("Next View and Previous View move between four views; choose four views in Layout on the View page.")
										 : Text::tr("Next View and Previous View move between four views; %1 turns them on.").arg(keys));
		return false;
	}
	const int quadrant = ((m_activePane + step) % 4 + 4) % 4;
	activatePane(quadrant, step > 0 ? Qt::TabFocusReason : Qt::BacktabFocusReason);
	// The preview keeps focus as it moves, so a screen reader hears no focus
	// change: name the view that is now active.
	const QString message = Text::tr("Active view: %1.").arg(modelPaneViewDisplayName(m_paneViews.value(m_activePane)));
	m_status->setText(message);
	announceStatus(m_preview, message, LiveStatus::Result);
	return true;
}

void ModelEditorDialog::syncPanes()
{
	if (m_viewLayout == ModelViewLayout::FourViews && m_paneSync && !m_mirrors.isEmpty() && !m_paneSync->isActive())
		m_paneSync->start();
}

bool ModelEditorDialog::eventFilter(QObject *watched, QEvent *event)
{
	auto *mirror = qobject_cast<ModelViewport *>(watched);
	if (!mirror || !m_mirrors.contains(mirror))
		return QDialog::eventFilter(watched, event);
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
	{
		auto *mouse = static_cast<QMouseEvent *>(event);
		// Navigation in an inactive view stays there; editing clicks make it
		// the active view first, as 3ds Max does.
		if (mouse->button() != Qt::LeftButton && mouse->button() != Qt::RightButton)
			return false;
		int quadrant = -1;
		for (int index = 0; index < 4; ++index)
		{
			if (m_paneFrames[index] && m_paneFrames[index]->isAncestorOf(mirror))
				quadrant = index;
		}
		const QPointF global = mouse->globalPosition();
		activatePane(quadrant);
		m_pressForwardSource = mirror;
		QMouseEvent press(mouse->type(), m_preview->mapFromGlobal(global), global, mouse->button(), mouse->buttons(), mouse->modifiers());
		QCoreApplication::sendEvent(m_preview, &press);
		return true;
	}
	case QEvent::MouseMove:
	case QEvent::MouseButtonRelease:
	{
		// The pressed widget keeps the mouse until release: hand its events on.
		if (m_pressForwardSource != mirror)
			return false;
		auto *mouse = static_cast<QMouseEvent *>(event);
		const QPointF global = mouse->globalPosition();
		QMouseEvent forwarded(mouse->type(), m_preview->mapFromGlobal(global), global, mouse->button(), mouse->buttons(), mouse->modifiers());
		QCoreApplication::sendEvent(m_preview, &forwarded);
		if (event->type() == QEvent::MouseButtonRelease && mouse->buttons() == Qt::NoButton)
			m_pressForwardSource.clear();
		return true;
	}
	default:
		return false;
	}
}

} // namespace vibestudio
