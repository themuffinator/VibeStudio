// NetRadiant Custom's layer commands on the Levels page: Expand Selection to
// Layers and its layers menu, which TrenchBroom offers as Move to Layer in the
// view context menu. Both go through core/level_scene_layers.h, the service
// `editor scene select --expand-layers` and `editor scene move-to-layer` use.
//
// Workflow reference: NRC radiant/layerswindow.cpp Scene_ExpandSelectionToLayers
// and context_moveto_menu at 449778b3 (GPL-2.0-or-later); TB MapViewBase's Move
// to Layer menu at 4c756be1 (GPL-3.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "app/studio_icons.h"
#include "core/level_object_selection.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QMenu>
#include <QStatusBar>

#include <memory>

namespace vibestudio {

void ApplicationShell::registerLevelSceneLayerCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& tip, std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Edit;
		command.label = label;
		command.statusTip = tip;
		command.iconName = QStringLiteral("layers");
		command.menuSection = tr("Layers");
		command.handler = [this, handler = std::move(handler)]() {
			setMode(StudioMode::Levels);
			handler();
		};
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.expandSelectionToLayers"), tr("Expand Selection to Layers"),
		tr("Add every visible, unlocked object in the layers of the selected objects, nested groups included, to the selection, "
		   "as NetRadiant Custom does."),
		[this]() { selectLevelMapObjectsFromUi(LevelObjectSelectionMode::Layers); });
	add(QStringLiteral("map.moveToLayer"), tr("Move to Layer…"),
		tr("Choose a layer at the pointer and move the selection into it as one undo step. Closed groups and brush entities move whole."),
		[this]() { showLevelMoveToLayerPopup(); });
}

void ApplicationShell::refreshLevelSceneLayerCommands()
{
	if (!m_commands) {
		return;
	}
	// The menu itself says why a layer is unavailable, so the command only
	// needs a map and a selection, and no tool in the middle of an edit: the
	// same rule as hiding the selection, since a move into a hidden layer hides.
	m_commands->setEnabled(QStringLiteral("map.moveToLayer"), canChangeLevelVisibility(LevelVisibilityOperation::HideSelected));
}

QMenu* ApplicationShell::buildLevelMoveToLayerMenu(QWidget* parent)
{
	auto* menu = new QMenu(tr("Move to Layer"), parent);
	menu->setObjectName(QStringLiteral("levelMoveToLayerMenu"));
	menu->setIcon(studioIcon(QStringLiteral("layers")));
	menu->setAccessibleName(tr("Move the selection to a layer"));
	menu->setToolTipsVisible(true);
	const LevelMapDocument& document = m_levelMapDocument;
	const QSet<QString> locked = levelSceneLockedNodes(document.scene);
	QStringList layers {QString()};
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (node.kind == LevelSceneNodeKind::Layer) {
			layers << node.id;
		}
	}
	// One pass over the selection serves every entry.
	const QStringList reasons = levelMapSelectionLayerMoveBlockedReasons(document, layers);
	bool available = false;
	for (qsizetype index = 0; index < layers.size(); ++index) {
		const QString& id = layers.at(index);
		const LevelSceneNode* node = levelSceneNode(document.scene, id);
		const QString name = node ? node->name : tr("Default");
		QString label = name;
		// A layer name is plain text, not a mnemonic.
		label.replace(QLatin1Char('&'), QStringLiteral("&&"));
		if (node && !node->visible) {
			label = tr("%1 · Hidden", "layer name, then its state").arg(label);
		}
		if (locked.contains(id)) {
			label = tr("%1 · Locked", "layer name, then its state").arg(label);
		}
		QAction* action = menu->addAction(label);
		action->setObjectName(QStringLiteral("levelMoveToLayer-") + (id.isEmpty() ? QStringLiteral("default") : id));
		const QString reason = reasons.value(index);
		action->setEnabled(reason.isEmpty());
		action->setToolTip(reason.isEmpty() ? tr("Move the selection into %1 as one undo step.").arg(name) : reason);
		action->setStatusTip(action->toolTip());
		connect(action, &QAction::triggered, this, [this, id]() { moveLevelSelectionToLayerFromUi(id); });
		available = available || reason.isEmpty();
	}
	menu->menuAction()->setEnabled(available && canChangeLevelVisibility(LevelVisibilityOperation::HideSelected));
	return menu;
}

void ApplicationShell::showLevelMoveToLayerPopup()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		statusBar()->showMessage(tr("Open a map before moving objects to a layer."), 5000);
		return;
	}
	std::unique_ptr<QMenu> menu(buildLevelMoveToLayerMenu(this));
	menu->setObjectName(QStringLiteral("levelMoveToLayerPopup"));
	if (!canChangeLevelVisibility(LevelVisibilityOperation::HideSelected)) {
		statusBar()->showMessage(tr("Select objects and finish the active tool before moving the selection to a layer."), 6000);
		return;
	}
	if (!menu->menuAction()->isEnabled()) {
		// Every entry is greyed out, as NetRadiant Custom's are; say why once,
		// preferring a reason other than the selection's own layer.
		const QList<QAction*> entries = menu->actions();
		const QString own = entries.isEmpty() ? QString() : entries.constFirst()->toolTip();
		QString reason = own;
		for (const QAction* entry : entries) {
			if (entry->toolTip() != own) {
				reason = entry->toolTip();
				break;
			}
		}
		statusBar()->showMessage(reason, 6000);
	}
	// NetRadiant Custom opens the menu at the pointer. A pointer outside the
	// window, as after a key press from elsewhere, opens it on the focused view.
	QPoint at = QCursor::pos();
	if (!frameGeometry().contains(at)) {
		QWidget* focus = QApplication::focusWidget();
		at = focus && isAncestorOf(focus) ? focus->mapToGlobal(focus->rect().center()) : mapToGlobal(rect().center());
	}
	menu->exec(at);
}

void ApplicationShell::moveLevelSelectionToLayerFromUi(const QString& layerId)
{
	const LevelSceneNode* node = levelSceneNode(m_levelMapDocument.scene, layerId);
	if (!layerId.isEmpty() && (!node || node->kind != LevelSceneNodeKind::Layer)) {
		statusBar()->showMessage(tr("That layer no longer exists. Choose another layer."), 6000);
		refreshLevelMapWorkbench();
		return;
	}
	const QString name = node ? node->name : tr("Default");
	if (m_levelObjectSelectionBusy) {
		statusBar()->showMessage(tr("Wait for the current selection to finish before moving it to a layer."), 5000);
		return;
	}
	if (!canChangeLevelVisibility(LevelVisibilityOperation::HideSelected)) {
		statusBar()->showMessage(tr("Select objects and finish the active tool before moving the selection to a layer."), 6000);
		return;
	}
	LevelSceneLayerMoveReport report;
	QString error;
	if (!moveLevelMapSelectionToLayer(&m_levelMapDocument, layerId, &report, &error)) {
		statusBar()->showMessage(tr("Could not move the selection to %1: %2").arg(name, error), 7000);
		refreshLevelMapWorkbench();
		return;
	}
	if (!report.changed) {
		statusBar()->showMessage(tr("The selection is already in %1; nothing changed.").arg(name), 5000);
		return;
	}
	recordActivity(tr("Moved to layer"), name, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Moved the selection to %1 as one undo step.").arg(name);
	if (const int groups = static_cast<int>(report.groups.size()); groups > 0) {
		message += QLatin1Char(' ') + tr("%n closed group(s) moved whole.", nullptr, groups);
	}
	if (report.deselected > 0) {
		message += QLatin1Char(' ') + tr("%n object(s) are hidden in that layer and left the selection.", nullptr, report.deselected);
	}
	statusBar()->showMessage(message, 7000);
}

} // namespace vibestudio
