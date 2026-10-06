#include "app/application_shell.h"
#include "app/level_udmf_dialog.h"
#include <QStatusBar>

namespace vibestudio {
void ApplicationShell::editLevelUdmfPropertiesFromUi() {
	if (!m_levelMapDocument.doomUdmf) {
		return;
	}
	LevelUdmfDialog dialog(m_levelMapDocument, this);
	const auto publish = levelPlacementCommitter();
	dialog.setApplyHandler([this, publish](const LevelMapDocument& candidate, QString* error) {
		if (!publish(candidate, error)) {
			return false;
		}
		recordActivity(tr("UDMF properties edited"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
					   tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("UDMF properties updated. Save the map and rebuild nodes before testing."));
		return true;
	});
	dialog.exec();
}
} // namespace vibestudio
