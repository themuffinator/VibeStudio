#include "app/application_shell.h"
#include "app/level_find_replace_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "core/level_material_find.h"
#include "core/level_scene.h"

#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QStatusBar>
#include <QTimer>

// Reveal Material and Find and Replace Textures. Workflow references:
// TrenchBroom MapWindow::revealMaterial and MaterialBrowser::revealMaterial
// at 4c756be1 (GPL-3.0-or-later), and NetRadiant Custom FindTextureDialog and
// FindReplaceTextures at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.
namespace vibestudio {
namespace {
// What the map view hides with filters or Hide, as selectors: Find and
// Replace leaves those objects alone, as Select All does.
QSet<QString> viewHiddenObjects(const LevelMapDocument& document, const MapViewport* view)
{
	QSet<QString> result;
	if (!view) { return result; }
	for (const QString& key : levelSceneObjects(document)) {
		const qsizetype colon = key.indexOf(QLatin1Char(':'));
		if (view->isHidden(levelMapSelectionKindFromId(key.left(colon)), key.mid(colon + 1).toInt())) { result.insert(key); }
	}
	return result;
}

LevelMaterialFindScope coreScope(LevelFindReplaceDialog::Scope scope)
{
	switch (scope) {
	case LevelFindReplaceDialog::Scope::Selection: return LevelMaterialFindScope::Selection;
	case LevelFindReplaceDialog::Scope::Picks: return LevelMaterialFindScope::Picks;
	default: return LevelMaterialFindScope::Map;
	}
}
} // namespace

void ApplicationShell::levelMaterialPicks(QVector<LevelSurfaceFace>* faces, QVector<int>* patches) const
{
	if (faces) { *faces = levelPickedSurfaceFaces(); }
	if (!patches) { return; }
	patches->clear();
	for (const LevelMapSelectionRef& ref : m_levelMapDocument.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakePatch && m_levelTexturedPatches.contains(ref.objectId)) { patches->append(ref.objectId); }
	}
}

void ApplicationShell::refreshLevelMaterialFindCommands()
{
	if (!m_commands) { return; }
	refreshLevelSmartTagCommands();
	LevelSelectionMaterial shared;
	if (m_levelMapDocument.format != LevelMapFormat::Unknown) {
		QVector<LevelSurfaceFace> faces;
		QVector<int> patches;
		levelMaterialPicks(&faces, &patches);
		shared = levelSelectionMaterial(m_levelMapDocument, faces, patches);
	}
	m_commands->setEnabled(QStringLiteral("map.revealMaterial"), shared.state == LevelSelectionMaterial::State::Single);
	// A mixed selection disables the command; its status tip says why in the
	// status bar, the Tools tab and to screen readers.
	if (QAction* action = m_commands->action(QStringLiteral("map.revealMaterial"))) {
		QString tip;
		if (shared.state == LevelSelectionMaterial::State::Single) {
			tip = tr("Show %1 on the Textures tab and make it the current material.").arg(shared.material);
		} else if (shared.state == LevelSelectionMaterial::State::Mixed) {
			tip = tr("Unavailable: the selection mixes %1 and %2. Select surfaces that share one material.").arg(shared.material, shared.other);
		} else {
			tip = tr("Select brushes, patches or faces that share one material to show it on the Textures tab.");
		}
		if (action->statusTip() != tip) {
			action->setStatusTip(tip);
			action->setToolTip(tip);
		}
	}
	if (m_levelFindReplaceDialog && m_levelFindReplaceDialog->isVisible() && m_levelFindReplaceRefresh) { m_levelFindReplaceRefresh->start(); }
}

void ApplicationShell::revealLevelMapMaterialFromUi()
{
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	levelMaterialPicks(&faces, &patches);
	const LevelSelectionMaterial shared = levelSelectionMaterial(m_levelMapDocument, faces, patches);
	if (shared.state == LevelSelectionMaterial::State::Mixed) {
		statusBar()->showMessage(tr("The selection mixes %1 and %2, so there is no single material to reveal.").arg(shared.material, shared.other), 6000);
		return;
	}
	if (shared.state != LevelSelectionMaterial::State::Single || !m_levelMapTextures) {
		statusBar()->showMessage(tr("Select brushes, patches or faces that share one material to reveal it."), 6000);
		return;
	}
	if (currentMode() != StudioMode::Levels) { setMode(StudioMode::Levels); }
	showLevelSidebarPage(QStringLiteral("textures"));
	// A Doom name can have a flat tile and a wall tile; take the one the
	// selection uses it as.
	const auto tile = [this, &shared]() -> QListWidgetItem* {
		QListWidgetItem* fallback = nullptr;
		for (int row = 0; row < m_levelMapTextures->count(); ++row) {
			QListWidgetItem* item = m_levelMapTextures->item(row);
			if (item->data(Qt::UserRole).toString().trimmed().compare(shared.material, Qt::CaseInsensitive) != 0) { continue; }
			const bool flatTile = item->data(Qt::UserRole + 12).toString().startsWith(QStringLiteral("flats/"));
			if (shared.flat == shared.wall || flatTile == shared.flat) { return item; }
			if (!fallback) { fallback = item; }
		}
		return fallback;
	};
	QListWidgetItem* item = tile();
	if (!item) {
		refreshLevelMapTextures();
		item = tile();
	}
	if (!item) {
		statusBar()->showMessage(tr("%1 is not on the Textures tab.").arg(shared.material), 6000);
		return;
	}
	// As TrenchBroom's browser does, a filter that hides the tile is cleared.
	if (item->isHidden() && m_levelMapTextureFilter) { m_levelMapTextureFilter->clear(); }
	const QString material = item->data(Qt::UserRole).toString();
	m_levelMapTextures->setCurrentItem(item);
	item->setSelected(true);
	m_levelMapTextures->scrollToItem(item, QAbstractItemView::PositionAtCenter);
	chooseLevelPaintMaterial(material);
	fillLevelFindReplaceField(material);
	statusBar()->showMessage(tr("Revealed %1 on the Textures tab; it is now the current material.").arg(material), 6000);
}

void ApplicationShell::showReplaceTextureDialog(const QString& replacement)
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || m_levelMapDocument.textureReferences.isEmpty()) {
		statusBar()->showMessage(tr("Open a map that uses textures to find or replace one."));
		return;
	}
	if (!m_levelFindReplaceDialog) {
		auto* dialog = new LevelFindReplaceDialog(this);
		m_levelFindReplaceDialog = dialog;
		m_levelFindReplaceRefresh = new QTimer(dialog);
		m_levelFindReplaceRefresh->setSingleShot(true);
		m_levelFindReplaceRefresh->setInterval(30);
		m_levelFindReplaceNamesSerial = ~quint64(0);
		connect(m_levelFindReplaceRefresh, &QTimer::timeout, this, &ApplicationShell::refreshLevelFindReplaceDialog);
		connect(dialog, &LevelFindReplaceDialog::inputsChanged, this, [this] {
			if (m_levelFindReplaceDialog) { m_levelFindReplaceScope = static_cast<int>(m_levelFindReplaceDialog->scope()); }
			if (m_levelFindReplaceRefresh) { m_levelFindReplaceRefresh->start(); }
		});
		connect(dialog, &LevelFindReplaceDialog::findRequested, this, &ApplicationShell::findLevelMaterialFromDialog);
		connect(dialog, &LevelFindReplaceDialog::replaceRequested, this, &ApplicationShell::replaceLevelMaterialFromDialog);
		// The Textures surface fills it too, in the map's own naming.
		if (m_textureEntries) {
			connect(m_textureEntries, &QListWidget::itemSelectionChanged, dialog, [this] {
				const QListWidgetItem* item = m_textureEntries->currentItem();
				if (item && item->isSelected()) { fillLevelFindReplaceField(mapTextureNameForEntry(item->data(Qt::UserRole).toString())); }
			});
		}
		dialog->setScope(static_cast<LevelFindReplaceDialog::Scope>(m_levelFindReplaceScope));
	}
	LevelFindReplaceDialog* dialog = m_levelFindReplaceDialog;
	refreshLevelFindReplaceDialog();
	// Start from what the selection shows, as Replace Texture always has.
	if (dialog->findText().trimmed().isEmpty()) {
		QVector<LevelSurfaceFace> faces;
		QVector<int> patches;
		levelMaterialPicks(&faces, &patches);
		const LevelSelectionMaterial shared = levelSelectionMaterial(m_levelMapDocument, faces, patches);
		QString preset = shared.state == LevelSelectionMaterial::State::None ? QString() : shared.material;
		if (preset.isEmpty()) {
			const QVector<LevelMapTextureUse> usage = levelMapTextureUsage(m_levelMapDocument);
			preset = usage.isEmpty() ? QString() : usage.first().name;
		}
		dialog->setFindText(preset);
	}
	if (!replacement.isEmpty()) { dialog->setReplaceText(replacement); }
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
	// With the replacement given, the choice left is what to replace.
	if (replacement.isEmpty()) {
		dialog->findChild<QWidget*>(QStringLiteral("replaceTextureTo"))->setFocus(Qt::OtherFocusReason);
	} else {
		dialog->findChild<QWidget*>(QStringLiteral("replaceTextureFrom"))->setFocus(Qt::OtherFocusReason);
	}
	refreshLevelFindReplaceDialog();
}

void ApplicationShell::refreshLevelFindReplaceDialog()
{
	LevelFindReplaceDialog* dialog = m_levelFindReplaceDialog;
	if (!dialog) { return; }
	const bool hasMap = m_levelMapDocument.format != LevelMapFormat::Unknown;
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (m_levelFindReplaceNamesSerial != m_levelMapLoadSerial || m_levelFindReplaceNamesRevision != m_levelMapDocument.revision) {
		QStringList names;
		for (const LevelMapTextureUse& use : levelMapTextureUsage(m_levelMapDocument)) { names << use.name; }
		dialog->setTextureNames(names);
		m_levelFindReplaceNamesSerial = m_levelMapLoadSerial;
		m_levelFindReplaceNamesRevision = m_levelMapDocument.revision;
	}
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	levelMaterialPicks(&faces, &patches);
	dialog->setScopeAvailable(LevelFindReplaceDialog::Scope::Selection, !m_levelMapDocument.selection.isEmpty(), tr("Select objects first."));
	dialog->setScopeAvailable(LevelFindReplaceDialog::Scope::Picks, quake && (!faces.isEmpty() || !patches.isEmpty()),
		quake ? tr("Pick faces first, with Select Textured or in the camera.") : tr("Picked faces belong to Quake-family maps."));
	if (!hasMap) {
		dialog->setPreview(tr("Open a map that uses textures."), false, false);
		return;
	}
	LevelMaterialReplaceRequest request;
	request.find = dialog->findText();
	request.replace = dialog->replaceText();
	request.scope = coreScope(dialog->scope());
	request.faces = faces;
	request.patches = patches;
	request.excludedObjects = viewHiddenObjects(m_levelMapDocument, m_levelMapViewport);
	LevelMaterialReplaceReport report;
	QString error;
	if (!previewLevelMaterialReplace(m_levelMapDocument, request, &report, &error)) {
		dialog->setPreview(error, false, false);
		return;
	}
	const QString find = request.find.trimmed();
	const QString replacement = request.replace.trimmed();
	const int matches = static_cast<int>(report.targets.size());
	// Find leaves closed groups out on Quake-family maps, as Select Textured does.
	const int findable = quake ? matches - report.inClosedGroups : matches;
	const bool canFind = findable > 0 && request.scope != LevelMaterialFindScope::Picks;
	QString text;
	if (replacement.isEmpty()) {
		text = matches == 0 ? tr("Nothing in scope uses %1.").arg(find) : tr("%n surface(s) in scope use %1.", nullptr, matches).arg(find);
		if (quake && report.inClosedGroups > 0) {
			text += QLatin1Char(' ') + tr("Find leaves out %n in closed groups; open a group to search inside it.", nullptr, report.inClosedGroups);
		}
		if (request.scope == LevelMaterialFindScope::Picks) {
			text += QLatin1Char(' ') + tr("Find searches the whole map or the selected objects.");
		}
	} else if (report.changes > 0) {
		text = tr("%n use(s) will change.", nullptr, report.changes);
	} else {
		text = matches == 0 ? tr("Nothing in scope uses %1.").arg(find) : tr("Everything in scope already uses %1.").arg(replacement);
	}
	if (report.skipped > 0) {
		text += QLatin1Char(' ') + tr("%n hidden, locked or out-of-context use(s) stay as they are.", nullptr, report.skipped);
	}
	dialog->setPreview(text, canFind, !replacement.isEmpty() && report.changes > 0);
}

void ApplicationShell::findLevelMaterialFromDialog()
{
	LevelFindReplaceDialog* dialog = m_levelFindReplaceDialog;
	if (!dialog) { return; }
	const QString find = dialog->findText().trimmed();
	const auto scope = dialog->scope();
	if (find.isEmpty()) {
		statusBar()->showMessage(tr("Name the texture to find."), 6000);
		return;
	}
	if (scope == LevelFindReplaceDialog::Scope::Picks) {
		statusBar()->showMessage(tr("Find searches the whole map or the selected objects; choose one of those."), 6000);
		return;
	}
	if (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map) {
		// NRC's search mode selects faces; here they join the independent
		// face picks Select Textured makes, and matching patches are selected.
		selectLevelMapTexturedFromUi(find, scope == LevelFindReplaceDialog::Scope::Selection);
	} else if (m_levelMapDocument.format == LevelMapFormat::DoomWad) {
		if (scope == LevelFindReplaceDialog::Scope::Map) {
			selectLevelMapObjectsUsingTexture(find);
		} else {
			QSet<QString> selected;
			for (const LevelMapSelectionRef& ref : m_levelMapDocument.selection) { selected.insert(levelMapSelectionRefId(ref)); }
			QVector<LevelMapSelectionRef> candidates = levelMapObjectsUsingTexture(m_levelMapDocument, find);
			candidates.removeIf([&selected](const LevelMapSelectionRef& ref) { return !selected.contains(levelMapSelectionRefId(ref)); });
			int leftHidden = 0;
			const QVector<LevelMapSelectionRef> objects = visibleLevelMapObjects(candidates, &leftHidden);
			if (objects.isEmpty()) {
				statusBar()->showMessage(tr("Nothing selected uses %1.").arg(find), 6000);
				return;
			}
			setLevelMapUserSelection(objects);
			refreshLevelMapWorkbench();
			const int chosen = static_cast<int>(objects.size());
			statusBar()->showMessage(tr("Selected the %n object(s) that use %1.", nullptr, chosen).arg(find));
		}
	}
	if (m_levelFindReplaceRefresh) { m_levelFindReplaceRefresh->start(); }
}

void ApplicationShell::replaceLevelMaterialFromDialog()
{
	LevelFindReplaceDialog* dialog = m_levelFindReplaceDialog;
	if (!dialog) { return; }
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy()) {
		statusBar()->showMessage(tr("Finish or cancel the pending surface edit before replacing textures."), 6000);
		return;
	}
	reconcileLevelTexturedSelection();
	LevelMaterialReplaceRequest request;
	request.find = dialog->findText();
	request.replace = dialog->replaceText();
	request.scope = coreScope(dialog->scope());
	levelMaterialPicks(&request.faces, &request.patches);
	request.excludedObjects = viewHiddenObjects(m_levelMapDocument, m_levelMapViewport);
	LevelMaterialReplaceReport report;
	QString error;
	if (!replaceLevelMapMaterial(&m_levelMapDocument, request, &report, &error)) {
		statusBar()->showMessage(tr("Could not replace the texture: %1").arg(error), 6000);
		refreshLevelFindReplaceDialog();
		return;
	}
	const QString source = request.find.trimmed();
	const QString target = request.replace.trimmed();
	recordActivity(tr("Level map texture replaced"), tr("%1 to %2").arg(source, target), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Replaced %1 with %2 on %n use(s).", nullptr, report.replaced).arg(source, target);
	if (report.skipped > 0) {
		message += QLatin1Char(' ') + tr("%n hidden, locked or out-of-context use(s) stay as they are.", nullptr, report.skipped);
	}
	statusBar()->showMessage(message);
	refreshLevelFindReplaceDialog();
}

bool ApplicationShell::fillLevelFindReplaceField(const QString& material)
{
	LevelFindReplaceDialog* dialog = m_levelFindReplaceDialog;
	if (!dialog || !dialog->isVisible() || material.trimmed().isEmpty()) { return false; }
	const bool replacing = dialog->fillFocusedField(material);
	statusBar()->showMessage(replacing ? tr("Replace with now reads %1.").arg(material) : tr("Find now reads %1.").arg(material), 6000);
	return true;
}

} // namespace vibestudio
