#include "app/application_shell.h"
#include "app/level_gestures_dialog.h"

namespace vibestudio {
void ApplicationShell::showLevelGesturePreferences()
{
	LevelGesturesDialog dialog(m_settings, m_settings.selectedEditorProfileId(), this);
	connect(&dialog, &LevelGesturesDialog::preferencesApplied, this, [this] { applyLevelEditorProfile(false, true); });
	dialog.exec();
	if (m_levelControlsDialog && m_levelControlsDialog->isVisible()) { showLevelControlsReference(); }
}
}
