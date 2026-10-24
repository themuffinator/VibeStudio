#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "core/level_entity_connections.h"
#include <QStatusBar>

namespace vibestudio {
LevelEntityConnectionRequest ApplicationShell::levelEntityConnectionRequest(bool kill) const
{
	LevelEntityConnectionRequest request;
	request.mode = kill ? LevelEntityConnectionMode::RadiantKillTarget : m_levelControls.entityConnectionMode;
	GameInstallationProfile installation;
	const auto game = effectiveProjectGameKey(currentProjectManifestOrDefault(),selectedGameInstallation(&installation) ? &installation : nullptr);
	request.game = levelEntityConnectionGame(m_levelMapDocument,game);
	return request;
}
void ApplicationShell::connectLevelMapEntitiesFromUi(bool kill)
{
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::ConnectEntities;
	request.connection = levelEntityConnectionRequest(kill);
	const auto publish = levelPlacementCommitter();
	const auto result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,request);
	if (!result.succeeded) { statusBar()->showMessage(tr("Could not connect: %1").arg(result.error)); return; }
	const auto current = levelEntityConnectionRequest(kill);
	if (current.mode != request.connection.mode || current.game != request.connection.game) {
		statusBar()->showMessage(tr("The connection profile or game changed. Run Connect again from the current state.")); return;
	}
	QString error;
	if (!publish(result.document,&error)) { statusBar()->showMessage(tr("Could not connect: %1").arg(error)); return; }
	const auto& report = result.connectionReport;
	if (!report.changedEntities) { statusBar()->showMessage(tr("The connection to %1 is already present.").arg(report.targetName)); return; }
	recordActivity(kill ? tr("Level map entities kill-connected") : tr("Level map entities connected"), report.targetName,
		QStringLiteral("level-map"),OperationState::Warning,tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Connected through %1 to %2.").arg(report.property,report.targetName));
}
} // namespace vibestudio
