#include "app/application_shell.h"
#include "app/level_bookmarks_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include <QAction>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QToolButton>
#include <QUuid>

namespace vibestudio {
namespace {
bool failed(QString* error, const QString& message)
{
	if (error) { *error = message; }
	return false;
}
QString storeFor(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::Unknown ? QString() : levelBookmarkStorePath(document.sourcePath,
		document.format == LevelMapFormat::DoomWad ? document.mapName : QString());
}
} // namespace

void ApplicationShell::refreshLevelBookmarkContext()
{
	if (m_levelBookmarkLoadSerial != m_levelMapLoadSerial) {
		restoreLevelViewWorkspace();
		m_levelBookmarkLoadSerial = m_levelMapLoadSerial;
		m_levelBookmarkCameraPending = false;
		m_levelViewLinks = m_settings.levelViewLinks();
		refreshLevelViewLinkActions();
		m_levelBookmarkCursor.clear();
		m_levelBookmarks.clear();
		m_levelBookmarkRevision.clear();
		m_levelBookmarkStore = storeFor(m_levelMapDocument);
		reloadLevelBookmarks();
	}
	if (m_levelBookmarksButton) { m_levelBookmarksButton->setEnabled(m_levelMapDocument.format != LevelMapFormat::Unknown); }
}
bool ApplicationShell::reloadLevelBookmarks(QString* error)
{
	LevelViewBookmarks loaded;
	QByteArray revision;
	QString problem;
	if (!m_levelBookmarkStore.isEmpty() && !readLevelBookmarks(m_levelBookmarkStore, &loaded, &revision, &problem)) {
		m_levelBookmarkError = problem;
		statusBar()->showMessage(problem);
		return failed(error, problem);
	}
	if (!m_levelBookmarkStore.isEmpty()) { m_levelBookmarks = std::move(loaded); }
	m_levelBookmarkRevision = revision;
	m_levelBookmarkError.clear();
	++m_levelBookmarkGeneration;
	refreshCommandEnablement();
	return true;
}
bool ApplicationShell::replaceLevelBookmarks(const LevelViewBookmarks& bookmarks, QString* error)
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) { return failed(error, tr("Open or create a map before saving views.")); }
	if (m_settings.isReadOnly()) { return failed(error, tr("The settings store is read-only.")); }
	if (!m_levelBookmarkError.isEmpty()) { return failed(error, m_levelBookmarkError); }
	if (!validateLevelBookmarks(bookmarks, error)) { return false; }
	if (!m_levelBookmarkStore.isEmpty() && !writeLevelBookmarks(m_levelBookmarkStore, bookmarks, m_levelBookmarkRevision,
		&m_levelBookmarkRevision, error)) { return false; }
	m_levelBookmarks = bookmarks;
	++m_levelBookmarkGeneration;
	refreshCommandEnablement();
	return true;
}
void ApplicationShell::copyLevelBookmarksAfterSave()
{
	const auto destination = storeFor(m_levelMapDocument);
	if (destination == m_levelBookmarkStore) { return; }
	m_levelBookmarkStore = destination;
	m_levelBookmarkRevision.clear();
	++m_levelBookmarkGeneration;
	LevelViewBookmarks existing;
	QString error;
	if (m_levelBookmarks.isEmpty()) { reloadLevelBookmarks(); return; }
	if (m_settings.isReadOnly()) { error = tr("The settings store is read-only."); }
	else if (readLevelBookmarks(destination, &existing, &m_levelBookmarkRevision, &error)) {
		if (!existing.isEmpty()) { error = tr("The destination map already has saved views. Export this list, or reload the destination's list in Saved Level Views."); }
		else { writeLevelBookmarks(destination, m_levelBookmarks, m_levelBookmarkRevision, &m_levelBookmarkRevision, &error); }
	}
	m_levelBookmarkError = error;
	if (!error.isEmpty()) {
		recordActivity(tr("Saved views were not copied"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning, error);
	}
}
bool ApplicationShell::captureLevelViewState(LevelViewState* state, QString* error)
{
	if (!state || m_levelMapDocument.format == LevelMapFormat::Unknown || m_levelPlanViews.size() != 3 || !m_levelMap3D) {
		return failed(error, tr("Open or create a map before saving views."));
	}
	if (!m_levelBookmarkCameraPending && (!m_levelMap3D->isEnabled() || m_levelMap3DSource != QString::number(m_levelMapLoadSerial))) {
		refreshLevelMap3D();
		return failed(error, tr("The level camera preview is loading. Capture the view when it finishes."));
	}
	LevelViewState captured;
	captured.layout = m_levelViewLayout;
	captured.activePlan = static_cast<int>(m_levelPlanViews.indexOf(m_levelMapViewport));
	// A temporary maximization must not turn a four-pane bookmark into a
	// layout with a permanently hidden camera.
	captured.cameraVisible = m_levelMaximized.view ? m_levelMaximized.visible.contains(m_levelMap3D) : levelMap3DShowing();
	for (int i = 0; i < 3; ++i) { captured.plans[i] = m_levelPlanViews[i]->navigationState(); }
	captured.camera = m_levelBookmarkCameraPending ? m_levelBookmarkCamera : m_levelMap3D->navigationState();
	captured.links = m_levelViewLinks;
	if (!validateLevelViewState(captured, error)) { return false; }
	*state = captured;
	return true;
}
bool ApplicationShell::restoreLevelViewState(const LevelViewState& state, QString* error)
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || m_levelPlanViews.size() != 3 || !m_levelMap3D) {
		return failed(error, tr("Open or create a map before restoring a view."));
	}
	if (!validateLevelViewState(state, error)) { return false; }
	const QScopedValueRollback<bool> restoring(m_applyingLevelViewNavigation, true);
	m_levelViewLinks = state.links;
	refreshLevelViewLinkActions();
	m_levelBookmarkLayout = true;
	activateLevelPlanViewport(m_levelPlanViews[state.activePlan]);
	applyLevelViewLayout(state.layout);
	setLevelMap3D(state.cameraVisible);
	for (int i = 0; i < 3; ++i) { m_levelPlanViews[i]->restoreNavigationState(state.plans[i]); }
	if (m_levelMapProjection) {
		const QSignalBlocker block(m_levelMapProjection);
		m_levelMapProjection->setCurrentIndex(state.plans[state.activePlan].projection);
	}
	m_levelBookmarkCamera = state.camera;
	m_levelBookmarkCameraPending = true;
	m_levelMap3D->restoreNavigationState(state.camera);
	// Framing a new asynchronous preview must not move a restored camera.
	if (m_levelMap3D->isEnabled() && m_levelMap3DSource == QString::number(m_levelMapLoadSerial)) { applyPendingLevelBookmarkCamera(); }
	else { refreshLevelMap3D(); }
	refreshLevelCameraMarker();
	return true;
}
void ApplicationShell::applyPendingLevelBookmarkCamera()
{
	if (!m_levelBookmarkCameraPending || m_levelBookmarkLoadSerial != m_levelMapLoadSerial) { return; }
	const QScopedValueRollback<bool> restoring(m_applyingLevelViewNavigation, true);
	m_levelMap3D->restoreNavigationState(m_levelBookmarkCamera);
	m_levelBookmarkCameraPending = false;
}
bool ApplicationShell::captureLevelBookmark(const QString& name, QString* error)
{
	LevelViewState view;
	if (!captureLevelViewState(&view, error)) { return false; }
	auto candidate = m_levelBookmarks;
	candidate.append({QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(), view});
	return replaceLevelBookmarks(candidate, error);
}
bool ApplicationShell::restoreLevelBookmark(const QString& id, QString* error)
{
	for (const auto& bookmark : m_levelBookmarks) {
		if (bookmark.id != id) { continue; }
		if (!restoreLevelViewState(bookmark.view, error)) { return false; }
		m_levelBookmarkCursor = id;
		statusBar()->showMessage(tr("Restored view: %1").arg(bookmark.name));
		return true;
	}
	return failed(error, tr("The saved view no longer exists. Reload the view list."));
}
void ApplicationShell::captureLevelBookmarkFromUi()
{
	bool accepted = false;
	const auto name = QInputDialog::getText(this, tr("Save Current Level View"), tr("View name"), QLineEdit::Normal, {}, &accepted);
	if (!accepted) { return; }
	QString error;
	if (!captureLevelBookmark(name, &error)) { statusBar()->showMessage(error); }
	else { statusBar()->showMessage(m_levelBookmarkStore.isEmpty() ? tr("View saved for this session. Saving the map keeps its views.") : tr("View saved: %1").arg(name.trimmed())); }
}
void ApplicationShell::stepLevelBookmark(int direction)
{
	if (m_levelBookmarks.isEmpty()) { return; }
	int current = direction > 0 ? -1 : 0;
	for (int i = 0; i < m_levelBookmarks.size(); ++i) { if (m_levelBookmarks[i].id == m_levelBookmarkCursor) { current = i; break; } }
	const int next = (current + direction + m_levelBookmarks.size()) % m_levelBookmarks.size();
	QString error;
	if (!restoreLevelBookmark(m_levelBookmarks[next].id, &error)) { statusBar()->showMessage(error); }
}
void ApplicationShell::populateLevelBookmarkMenu(QMenu* menu)
{
	menu->clear();
	for (const auto& id : {QStringLiteral("map.saveView"), QStringLiteral("map.manageViews"), QStringLiteral("map.nextSavedView"), QStringLiteral("map.previousSavedView")}) {
		if (auto* action = m_commands ? m_commands->action(id) : nullptr) { menu->addAction(action); }
	}
	menu->addSeparator();
	for (const auto& bookmark : m_levelBookmarks) {
		auto* action = menu->addAction(QString(bookmark.name).replace(QLatin1Char('&'), QStringLiteral("&&")));
		action->setData(bookmark.id);
		connect(action, &QAction::triggered, this, [this, id = bookmark.id] {
			QString error;
			if (!restoreLevelBookmark(id, &error)) { statusBar()->showMessage(error); }
		});
	}
}
void ApplicationShell::showLevelBookmarks()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) { return; }
	const auto serial = m_levelMapLoadSerial;
	auto generation = m_levelBookmarkGeneration;
	const auto check = [this, serial](QString* error) {
		return serial == m_levelMapLoadSerial || failed(error, tr("The open map changed. Close this manager and reopen it for the current map."));
	};
	LevelBookmarksDialog dialog(m_levelBookmarks,
		[this, check](LevelViewState* state, QString* error) { return check(error) && captureLevelViewState(state, error); },
		[this, check](const LevelViewState& state, QString* error) { return check(error) && restoreLevelViewState(state, error); },
		[this, check, &generation](const LevelViewBookmarks& views, QString* error) {
			if (!check(error)) { return false; }
			if (generation != m_levelBookmarkGeneration) { return failed(error, tr("The saved view list changed. Export your edits or reload the list before saving.")); }
			return replaceLevelBookmarks(views, error);
		},
		[this, check, &generation](LevelViewBookmarks* views, QString* error) {
			if (!check(error) || !reloadLevelBookmarks(error)) { return false; }
			*views = m_levelBookmarks;
			generation = m_levelBookmarkGeneration;
			return true;
		}, m_settings.isReadOnly(), this);
	if (!m_levelBookmarkError.isEmpty()) { dialog.setStatusMessage(m_levelBookmarkError); }
	dialog.exec();
}
} // namespace vibestudio
