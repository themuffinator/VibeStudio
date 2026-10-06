#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_layout.h"
#include "app/ui_primitives.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <initializer_list>

namespace vibestudio
{
MapViewport *ApplicationShell::createLevelPlanViewport(int index)
{
	auto *view = new MapViewport;
	view->setObjectName(index == 0 ? QStringLiteral("mapViewport") : QStringLiteral("mapViewport%1").arg(index));
	view->setAccessibleName(tr("Map orthographic pane %1").arg(index + 1));
	view->setProjection(static_cast<MapViewportProjection>(index));
	view->setMinimumSize(240, 180);
	view->installEventFilter(this);
	view->setContextMenuPolicy(Qt::CustomContextMenu);
	m_levelPlanViews.append(view);
	connect(view, &MapViewport::viewChanged, this, [this, view] {
		if (!m_applyingLevelViewLayout) { synchronizeLevelViewNavigation(view); }
	});
	connect(view, &MapViewport::selectionChanged, this, [this, view](int kind, int id) {
		activateLevelPlanViewport(view);
		selectLevelMapObjectFromViewport(kind, id);
	});
	connect(view, &MapViewport::selectionSetChanged, this, [this, view](const QVector<LevelMapSelectionRef> &selection) {
		activateLevelPlanViewport(view);
		syncLevelMapSelectionFromViewport(selection);
	});
	connect(view, &MapViewport::moveRequested, this, [this, view](double x, double y, double z) {
		activateLevelPlanViewport(view);
		moveLevelMapSelectionFromViewport(x, y, z);
	});
	connect(view, &MapViewport::resizeRequested, this, [this, view](const LevelMapVec3 &mins, const LevelMapVec3 &maxs) {
		activateLevelPlanViewport(view);
		resizeLevelMapSelectionFromViewport(mins, maxs);
	});
	connect(view, &MapViewport::clipRequested, this,
			[this, view](const LevelMapVec3 &a, const LevelMapVec3 &b, const LevelMapVec3 &c, int keep) {
				activateLevelPlanViewport(view);
				clipLevelMapSelectionFromViewport(a, b, c, keep);
			});
	connect(view, &MapViewport::sectorDrawRequested, this, [this, view](const QVector<QPointF> &corners) {
		activateLevelPlanViewport(view);
		drawLevelMapSectorFromViewport(corners);
	});
	connect(view, &MapViewport::paletteDropped, this, [this, view](const QString &payload, const QPointF &point) {
		activateLevelPlanViewport(view);
		placeFromLevelMapPalette(payload, point);
	});
	connect(view, &MapViewport::brushDrawRequested, this, [this, view](const LevelMapVec3 &mins, const LevelMapVec3 &maxs) {
		activateLevelPlanViewport(view);
		drawLevelMapBrushFromViewport(mins, maxs);
	});
	connect(view, &MapViewport::cameraAimRequested, this, [this, view](const QPointF &point) {
		activateLevelPlanViewport(view);
		aimLevelCameraAt(point, false);
	});
	connect(view, &MapViewport::cameraPlaceRequested, this, [this, view](const QPointF &point) {
		activateLevelPlanViewport(view);
		aimLevelCameraAt(point, true);
	});
	connect(view, &MapViewport::cameraDriveRequested, this, [this, view](double forward, double turn) {
		activateLevelPlanViewport(view);
		if (m_levelMap3D && m_levelMap3D->isPerspective()) {
			m_levelMap3D->moveCamera(forward, 0, 0, view->controls().fixedCameraSteps);
			m_levelMap3D->turnCamera(turn, 0);
		}
	});
	connect(view, &MapViewport::drawModeChanged, this, [this, view](bool enabled) {
		if (view != m_levelMapViewport) {
			return;
		}
		if (auto *action = m_commands ? m_commands->action(QStringLiteral("map.drawSector")) : nullptr) {
			action->setChecked(enabled);
		}
		statusBar()->showMessage(enabled ? tr("Draw Sector on: click the corners, then the first corner or Enter to close the shape.")
										 : tr("Draw Sector off."));
	});
	connect(view, &MapViewport::clipModeChanged, this, [this, view](bool enabled) {
		if (view != m_levelMapViewport) {
			return;
		}
		if (auto *action = m_commands ? m_commands->action(QStringLiteral("map.clipTool")) : nullptr) {
			action->setChecked(enabled);
		}
		statusBar()->showMessage(enabled ? tr("Clip tool on: drag a line across the brushes, Tab chooses what stays, Enter cuts.")
										 : tr("Clip tool off."));
	});
	connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint &point) {
		activateLevelPlanViewport(view);
		showLevelMapContextMenu(view->mapToGlobal(point), true);
	});
	connect(view, &MapViewport::hoverChanged, this, [this](const QString &summary) {
		if (m_levelMapHover) {
			m_levelMapHover->setText(summary.isEmpty() ? tr("Move the cursor over the map to inspect geometry.") : summary);
		}
	});
	return view;
}

void ApplicationShell::activateLevelPlanViewport(MapViewport *view)
{
	if (!m_applyingLevelViewLayout && view && m_levelPlanViews.contains(view)) {
		// Menus and the palette take focus; retain the pane they were opened from.
		m_levelLastFocusedView = view;
		if (m_levelMaximized.view && m_levelMaximized.view != view) { restoreLevelViewWorkspace(); }
	}
	if (m_applyingLevelViewLayout || !view || !m_levelPlanViews.contains(view) || view == m_levelMapViewport) {
		return;
	}
	if (m_levelMapViewport) {
		m_levelMapViewport->cancelInteraction();
		m_levelMapViewport->setClipMode(false);
		m_levelMapViewport->setDrawMode(false);
	}
	m_levelMapViewport = view;
	for (auto *pane : m_levelPlanViews) {
		pane->setActivePane(pane == view);
	}
	if (m_levelMapProjection) {
		const QSignalBlocker blocker(m_levelMapProjection);
		m_levelMapProjection->setCurrentIndex(static_cast<int>(view->projection()));
	}
	if (m_levelMapHover) {
		m_levelMapHover->setText(view->hoverSummary());
	}
	if (!m_applyingLevelViewLayout) {
		saveLevelMapViewState();
	}
	refreshCommandEnablement();
}

void ApplicationShell::synchronizeLevelPlanViews(bool frame)
{
	if (!m_levelMapViewport) {
		return;
	}
	const auto preferences = m_settings.accessibilityPreferences();
	for (auto *view : m_levelPlanViews) {
		const QSignalBlocker blocker(view);
		view->synchronizeSceneFrom(*m_levelMapViewport, frame);
		view->setGridSize(m_levelMapViewport->gridSize());
		view->setSnapToGrid(m_levelMapViewport->snapToGrid());
		view->setShowGrid(m_levelMapViewport->showGrid());
		view->setShowThings(m_levelMapViewport->showThings());
		view->setShowSectorFill(m_levelMapViewport->showSectorFill());
		view->setShowVertices(m_levelMapViewport->showVertices());
		view->setShowLabels(m_levelMapViewport->showLabels());
		view->setShowTargetLinks(m_levelMapViewport->showTargetLinks());
		view->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
		view->setReducedMotion(preferences.reducedMotion);
	}
	if (frame) { synchronizeLevelViewNavigation(m_levelMapViewport, true); }
}

void ApplicationShell::synchronizeLevelViewNavigation(MapViewport* source, bool fit)
{
	if (m_applyingLevelViewNavigation || m_buildingUi || m_levelPlanViews.size() != 3 || !source || !source->hasDocument()) { return; }
	std::array<PlanViewState, 3> states;
	for (int i = 0; i < 3; ++i) { states[i] = m_levelPlanViews[i]->navigationState(); }
	if (fit && m_levelViewLinks.zoom) {
		double scale = source->zoom();
		for (auto* view : m_levelPlanViews) { if (!view->QWidget::isHidden()) { scale = std::min(scale, view->zoom()); } }
		const int index = int(m_levelPlanViews.indexOf(source));
		if (index >= 0) { states[index].zoom = scale; }
	}
	if (!linkLevelPlanNavigation(&states, int(m_levelPlanViews.indexOf(source)), m_levelViewLinks)) { return; }
	const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
	for (int i = 0; i < 3; ++i) { m_levelPlanViews[i]->applyLinkedNavigation(states[i]); }
}

void ApplicationShell::frameLevelPlanViews(bool selection)
{
	{
		const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
		for (auto* view : m_levelPlanViews) {
			if (view->QWidget::isHidden()) { continue; }
			if (selection) { view->zoomToSelection(); } else { view->zoomToFit(); }
		}
	}
	// The smallest fitted scale keeps the selection visible in every pane.
	synchronizeLevelViewNavigation(m_levelMapViewport, true);
}

void ApplicationShell::followLevelCamera(bool force)
{
	if (m_applyingLevelViewNavigation || m_applyingLevelViewLayout || m_buildingUi || (!force && !m_levelViewLinks.followCamera)
		|| m_levelPlanViews.size() != 3 || !m_levelMap3D || !m_levelMap3D->isEnabled() || !m_levelMap3D->hasMesh()
		|| m_levelMap3DSource != QString::number(m_levelMapLoadSerial) || m_levelMapDocument.format == LevelMapFormat::Unknown) { return; }
	const auto camera = m_levelMap3D->navigationState();
	if (!validateCameraViewState(camera)) { return; }
	std::array<PlanViewState, 3> states;
	for (int i = 0; i < 3; ++i) { states[i] = m_levelPlanViews[i]->navigationState(); }
	if (!centerLevelPlanNavigation(&states, camera.perspective ? camera.position : camera.orbitTarget)) { return; }
	const QScopedValueRollback<bool> linking(m_applyingLevelViewNavigation, true);
	for (int i = 0; i < 3; ++i) { m_levelPlanViews[i]->applyLinkedNavigation(states[i]); }
}

void ApplicationShell::chooseLevelViewLinks(const LevelViewLinks& links)
{
	// A bookmark may override the other two choices for this map. Persist only
	// the choice the user changed, just like a partial CLI preferences update.
	auto defaults = m_settings.levelViewLinks();
	if (links.centers != m_levelViewLinks.centers) { defaults.centers = links.centers; }
	if (links.zoom != m_levelViewLinks.zoom) { defaults.zoom = links.zoom; }
	if (links.followCamera != m_levelViewLinks.followCamera) { defaults.followCamera = links.followCamera; }
	if (!m_settings.setLevelViewLinks(defaults)) {
		refreshLevelViewLinkActions();
		statusBar()->showMessage(tr("The navigation links could not be saved to these settings."));
		return;
	}
	m_levelViewLinks = links;
	refreshLevelViewLinkActions();
	synchronizeLevelViewNavigation(m_levelMapViewport);
	followLevelCamera();
	statusBar()->showMessage(tr("Plan centres: %1 · Plan zoom: %2 · Follow camera: %3")
		.arg(links.centers ? tr("linked") : tr("independent"), links.zoom ? tr("linked") : tr("independent"), links.followCamera ? tr("on") : tr("off")));
}

void ApplicationShell::refreshLevelViewLinkActions()
{
	if (!m_commands) { return; }
	if (m_levelViewLayoutButton && m_levelViewLayoutButton->menu()) {
		auto* menu = m_levelViewLayoutButton->menu();
		menu->setToolTipsVisible(true);
		bool separated = false;
		for (const auto& id : {QStringLiteral("map.linkPlanCenters"), QStringLiteral("map.linkPlanZoom"),
			QStringLiteral("map.followCamera"), QStringLiteral("map.centerPlansOnCamera")}) {
			auto* action = m_commands->action(id);
			if (!action || menu->actions().contains(action)) { continue; }
			if (action->isCheckable()) { action->setIconVisibleInMenu(false); }
			if (!separated) { menu->addSeparator(); separated = true; }
			menu->addAction(action);
		}
	}
	for (const auto& pair : {std::pair {"map.linkPlanCenters", m_levelViewLinks.centers}, std::pair {"map.linkPlanZoom", m_levelViewLinks.zoom},
		std::pair {"map.followCamera", m_levelViewLinks.followCamera}}) {
		if (auto* action = m_commands->action(QString::fromLatin1(pair.first))) {
			const QSignalBlocker block(action); action->setChecked(pair.second);
		}
	}
}

void ApplicationShell::saveLevelViewSplitters()
{
	if (m_applyingLevelViewLayout || m_levelMaximized.view || !m_levelMapUpperViews || !m_levelMapViews->isVisible()) {
		return;
	}
	const QString prefix = QStringLiteral("levelViews/%1/").arg(levelViewLayoutId(m_levelViewLayout));
	m_settings.setShellLayoutState(prefix + QStringLiteral("upper"), m_levelMapUpperViews->saveState());
	if (m_levelViewLayout == LevelViewLayout::FourViews) {
		m_settings.setShellLayoutState(prefix + QStringLiteral("root"), m_levelMapViews->saveState());
		m_settings.setShellLayoutState(prefix + QStringLiteral("lower"), m_levelMapLowerViews->saveState());
	}
}

void ApplicationShell::toggleLevelViewMaximized()
{
	if (m_levelMaximized.view) {
		restoreLevelViewWorkspace(true);
		statusBar()->showMessage(tr("View layout restored."), 3000);
		return;
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || !m_levelMapViews || !m_levelMap3D || !m_levelMapViewport) { return; }
	QList<QWidget*> panes {m_levelMap3D};
	for (auto* plan : m_levelPlanViews) { panes << plan; }
	QList<QWidget*> visiblePanes;
	for (auto* pane : panes) { if (!pane->isHidden()) { visiblePanes << pane; } }
	if (visiblePanes.size() < 2) { refreshLevelViewWorkspaceActions(); return; }
	auto* target = visiblePanes.contains(m_levelLastFocusedView) ? m_levelLastFocusedView
		: (visiblePanes.contains(m_levelMapViewport) ? static_cast<QWidget*>(m_levelMapViewport) : visiblePanes.first());
	saveLevelViewSplitters();
	m_levelMaximized.view = target;
	m_levelMaximized.root = m_levelMapViews->saveState();
	m_levelMaximized.upper = m_levelMapUpperViews->saveState();
	m_levelMaximized.lower = m_levelMapLowerViews->saveState();
	panes << m_levelMapUpperViews << m_levelMapLowerViews;
	if (m_levelPreviewStatus) { panes << m_levelPreviewStatus; }
	{
		const QScopedValueRollback<bool> applying(m_applyingLevelViewLayout, true);
		for (auto* pane : panes) { if (!pane->isHidden()) { m_levelMaximized.visible << pane; } }
		// Cancel unfinished manipulation before changing its coordinate surface.
		for (auto* plan : m_levelPlanViews) { plan->cancelInteraction(); }
		m_levelMap3D->hide(); // Ends fly, drag and paint gestures even when the camera is the target.
		for (auto* plan : m_levelPlanViews) { plan->setVisible(plan == target); }
		m_levelMap3D->setVisible(target == m_levelMap3D);
		m_levelMapUpperViews->setVisible(target->parentWidget() == m_levelMapUpperViews);
		m_levelMapLowerViews->setVisible(target->parentWidget() == m_levelMapLowerViews);
		if (m_levelPreviewStatus) { m_levelPreviewStatus->setVisible(target == m_levelMap3D); }
	}
	target->setFocus(Qt::ShortcutFocusReason);
	refreshLevelViewWorkspaceActions();
	refreshLevelCameraMarker();
	refreshCommandEnablement();
	statusBar()->showMessage(tr("Active view maximized. Restore View Layout returns to the previous pane sizes."), 4000);
}

void ApplicationShell::restoreLevelViewWorkspace(bool focus)
{
	if (!m_levelMaximized.view) { return; }
	const auto previous = m_levelMaximized;
	m_levelMaximized = {};
	{
		const QScopedValueRollback<bool> applying(m_applyingLevelViewLayout, true);
		QList<QWidget*> panes {m_levelMap3D, m_levelMapUpperViews, m_levelMapLowerViews};
		for (auto* plan : m_levelPlanViews) { plan->cancelInteraction(); panes << plan; }
		m_levelMap3D->hide();
		if (m_levelPreviewStatus) { panes << m_levelPreviewStatus; }
		for (auto* pane : panes) { pane->setVisible(previous.visible.contains(pane)); }
		m_levelMapViews->restoreState(previous.root);
		m_levelMapUpperViews->restoreState(previous.upper);
		m_levelMapLowerViews->restoreState(previous.lower);
	}
	if (focus && previous.view->isVisible()) { previous.view->setFocus(Qt::ShortcutFocusReason); }
	refreshLevelViewWorkspaceActions();
	refreshLevelCameraMarker();
	refreshCommandEnablement();
}

void ApplicationShell::equalizeLevelViews()
{
	restoreLevelViewWorkspace();
	if (!m_levelMapViews || m_levelMapDocument.format == LevelMapFormat::Unknown) { return; }
	{
		const QScopedValueRollback<bool> applying(m_applyingLevelViewLayout, true);
		for (auto* splitter : {m_levelMapViews, m_levelMapUpperViews, m_levelMapLowerViews}) {
			QList<int> sizes;
			const int extent = std::max(splitter->width(), splitter->height());
			for (int i = 0; i < splitter->count(); ++i) { sizes << extent; }
			splitter->setSizes(sizes);
		}
	}
	saveLevelViewSplitters();
	statusBar()->showMessage(tr("View sizes equalized."), 3000);
}

void ApplicationShell::refreshLevelViewWorkspaceActions()
{
	const bool cameraShowing = levelMap3DShowing();
	const bool planShowing = levelMap2DShowing();
	if (m_levelMap3DButton) { const QSignalBlocker block(m_levelMap3DButton); m_levelMap3DButton->setChecked(cameraShowing); }
	for (QWidget* control : std::initializer_list<QWidget*>{m_levelMapProjection, m_levelMapGrid, m_levelMapSnap,
		findChild<QToolButton*>(QStringLiteral("levelMapShowButton")), findChild<QToolButton*>(QStringLiteral("levelMapZoomSelection"))}) {
		if (control) { control->setEnabled(planShowing && m_levelMapDocument.format != LevelMapFormat::Unknown); }
	}
	if (!m_commands) { return; }
	if (auto* toggle = m_commands->action(QStringLiteral("map.toggle3D"))) { const QSignalBlocker block(toggle); toggle->setChecked(cameraShowing); }
	int visible = cameraShowing ? 1 : 0;
	for (auto* plan : m_levelPlanViews) { if (!plan->QWidget::isHidden()) { ++visible; } }
	for (const auto& id : {QStringLiteral("map.maximizeView"), QStringLiteral("map.equalizeViews")}) {
		auto* action = m_commands->action(id);
		if (!action) { continue; }
		const QSignalBlocker blocker(action);
		if (id == QLatin1String("map.maximizeView")) {
			action->setChecked(m_levelMaximized.view != nullptr);
			action->setText(m_levelMaximized.view ? tr("Restore View Layout") : tr("Maximize Active View"));
		}
		action->setEnabled(m_levelMapDocument.format != LevelMapFormat::Unknown && (m_levelMaximized.view || visible > 1));
		if (m_levelViewLayoutButton && m_levelViewLayoutButton->menu() && !m_levelViewLayoutButton->menu()->actions().contains(action)) {
			if (id == QLatin1String("map.maximizeView")) { m_levelViewLayoutButton->menu()->addSeparator(); }
			m_levelViewLayoutButton->menu()->addAction(action);
		}
	}
}

void ApplicationShell::chooseLevelViewLayout(const QString &preference)
{
	if (!m_settings.setLevelViewLayoutPreference(preference)) {
		// Triggering a checkable action toggles it before this callback. Keep
		// both menus truthful when a newer settings schema forbids the write.
		if (m_levelViewLayoutButton && m_levelViewLayoutButton->menu()) {
			const auto saved = m_levelBookmarkLayout ? levelViewLayoutId(m_levelViewLayout) : m_settings.levelViewLayoutPreference();
			for (auto *choice : m_levelViewLayoutButton->menu()->actions()) {
				const auto id = choice->data().toString();
				if (id != QStringLiteral("profile") && !levelViewLayoutForId(id, nullptr)) { continue; }
				choice->setChecked(id == saved);
				if (auto *action = m_commands ? m_commands->action(QStringLiteral("map.layout.") + id) : nullptr) {
					action->setChecked(id == saved);
				}
			}
		}
		statusBar()->showMessage(tr("The view layout could not be saved to these settings."));
		return;
	}
	LevelViewLayout layout = m_levelControls.layout;
	levelViewLayoutForId(preference, &layout);
	m_levelBookmarkLayout = false;
	applyLevelViewLayout(layout);
	statusBar()->showMessage(levelViewLayoutDisplayName(layout));
}

void ApplicationShell::applyLevelViewLayout(LevelViewLayout layout)
{
	if (!m_levelMapViews || !m_levelMapUpperViews || !m_levelMap3D || m_levelPlanViews.size() != 3) {
		return;
	}
	restoreLevelViewWorkspace();
	saveLevelViewSplitters();
	const QScopedValueRollback<bool> applying(m_applyingLevelViewLayout, true);
	m_levelViewLayout = layout;
	synchronizeLevelPlanViews();
	for (auto *view : m_levelPlanViews) {
		view->cancelInteraction();
		view->setClipMode(false);
		view->setDrawMode(false);
		view->hide();
	}
	m_levelMapUpperViews->insertWidget(0, m_levelMap3D);
	if (layout == LevelViewLayout::FourViews) {
		for (int i = 0; i < m_levelPlanViews.size(); ++i) {
			auto *view = m_levelPlanViews.at(i);
			view->setProjection(static_cast<MapViewportProjection>(i));
			(i == 0 ? m_levelMapUpperViews : m_levelMapLowerViews)->addWidget(view);
			view->show();
		}
		m_levelMapLowerViews->show();
	} else {
		// Keep exactly the camera and active plan in the upper splitter.
		// Hidden siblings must not consume persisted splitter slots.
		for (auto *view : m_levelPlanViews) {
			if (view != m_levelMapViewport) {
				m_levelMapLowerViews->addWidget(view);
			}
		}
		m_levelMapUpperViews->addWidget(m_levelMapViewport);
		m_levelMapLowerViews->hide();
	}
	const auto preference = m_levelBookmarkLayout ? levelViewLayoutId(layout) : m_settings.levelViewLayoutPreference();
	if (m_levelViewLayoutButton) {
		m_levelViewLayoutButton->setToolTip(levelViewLayoutDisplayName(layout));
		m_levelViewLayoutButton->setAccessibleDescription(m_levelViewLayoutButton->toolTip());
		for (auto *action : m_levelViewLayoutButton->menu()->actions()) {
			const auto id = action->data().toString();
			if (id == QStringLiteral("profile") || levelViewLayoutForId(id, nullptr)) { action->setChecked(id == preference); }
		}
	}
	if (m_commands) {
		for (const auto &id : {QStringLiteral("profile"), QStringLiteral("single-2d"), QStringLiteral("single-3d"),
							   QStringLiteral("camera-and-plan"), QStringLiteral("four-views")}) {
			if (auto *action = m_commands->action(QStringLiteral("map.layout.") + id)) {
				action->setChecked(id == preference);
			}
		}
	}
	if (m_levelMapProjection) {
		const QSignalBlocker blocker(m_levelMapProjection);
		m_levelMapProjection->setCurrentIndex(static_cast<int>(m_levelMapViewport->projection()));
	}
	setLevelMap3D(layout != LevelViewLayout::Single2D);
	const QString prefix = QStringLiteral("levelViews/%1/").arg(levelViewLayoutId(layout));
	const int width = std::max(2, m_levelMapViews->width());
	m_levelMapUpperViews->setSizes({width / 2, width / 2});
	const auto upper = m_settings.shellLayoutState(prefix + QStringLiteral("upper"));
	if (!upper.isEmpty()) {
		m_levelMapUpperViews->restoreState(upper);
	}
	if (layout == LevelViewLayout::FourViews) {
		const int height = std::max(2, m_levelMapViews->height());
		m_levelMapViews->setSizes({height / 2, height / 2});
		m_levelMapLowerViews->setSizes({width / 2, width / 2});
		const auto root = m_settings.shellLayoutState(prefix + QStringLiteral("root"));
		const auto lower = m_settings.shellLayoutState(prefix + QStringLiteral("lower"));
		if (!root.isEmpty()) {
			m_levelMapViews->restoreState(root);
		}
		if (!lower.isEmpty()) {
			m_levelMapLowerViews->restoreState(lower);
		}
	}
	for (auto *view : m_levelPlanViews) {
		view->setActivePane(view == m_levelMapViewport);
	}
	refreshLevelCameraMarker();
	synchronizeLevelViewNavigation(m_levelMapViewport);
}
} // namespace vibestudio
