// Set Entity Colour (NetRadiant Custom's K) and Normalise Entity Colour on the
// Levels page. Both write the _color key of the selected entities and of the
// entities holding the selected brushes and patches, as one undo step through
// core/level_entity_colour.h, the service the `map entity-colour` command uses.
//
// Workflow reference: NRC radiant/entity.cpp Entity_setColour and
// Entity_normalizeColor at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "core/level_entity_colour.h"

#include <QColorDialog>
#include <QStatusBar>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

QColor unitColour(const std::array<double, 3>& unit)
{
	const auto channel = [](double value) {
		return static_cast<float>(std::clamp(std::isfinite(value) ? value : 1.0, 0.0, 1.0));
	};
	return QColor::fromRgbF(channel(unit[0]), channel(unit[1]), channel(unit[2]));
}

} // namespace

void ApplicationShell::refreshLevelEntityColourCommands()
{
	if (!m_commands) {
		return;
	}
	m_commands->setEnabled(QStringLiteral("map.setEntityColour"), levelMapSelectionHasEntityColourTargets(m_levelMapDocument));
	m_commands->setEnabled(QStringLiteral("map.normaliseEntityColour"), levelMapSelectionEntityColourNormalisable(m_levelMapDocument));
}

void ApplicationShell::setLevelMapEntityColourFromUi()
{
	const LevelEntityColourTargets targets = levelMapEntityColourTargets(m_levelMapDocument);
	// Nothing to colour, or a locked target or one outside the open group, is
	// reported before the picker opens rather than after a colour is chosen.
	if (const QString blocked = levelMapEntityColourBlockedReason(m_levelMapDocument, targets); !blocked.isEmpty()) {
		statusBar()->showMessage(tr("Could not set the entity colour: %1").arg(blocked), 6000);
		return;
	}
	QString error;
	// The picker starts from the primary entity's colour, or else the colour
	// chosen last, as NetRadiant Custom's does.
	std::array<double, 3> seed = m_settings.levelEntityColour();
	LevelEntityColour current;
	if (levelMapPrimaryEntityColour(m_levelMapDocument, levelEntityColourKey(), &current)) {
		seed = current.unit();
	}
	const int count = static_cast<int>(targets.entities.size());
	QColorDialog dialog(unitColour(seed), this);
	dialog.setObjectName(QStringLiteral("levelEntityColourDialog"));
	dialog.setWindowTitle(tr("Entity Colour"));
	dialog.setAccessibleName(tr("Entity colour"));
	dialog.setAccessibleDescription(tr("The colour to write to the _color key of %n entit(y)(ies) as one undo step.", nullptr, count));
	if (dialog.exec() != QDialog::Accepted || !dialog.selectedColor().isValid()) {
		statusBar()->showMessage(tr("Entity colour left as it was."), 4000);
		return;
	}
	const QColor chosen = dialog.selectedColor();
	LevelEntityColourRequest request;
	request.unit = {chosen.redF(), chosen.greenF(), chosen.blueF()};
	LevelEntityColourReport report;
	if (!setLevelMapSelectionEntityColour(&m_levelMapDocument, request, &report, &error)) {
		statusBar()->showMessage(tr("Could not set the entity colour: %1").arg(error), 6000);
		refreshLevelMapWorkbench();
		return;
	}
	m_settings.setLevelEntityColour(request.unit);
	if (report.changed.isEmpty()) {
		statusBar()->showMessage(tr("The selected entities already have that colour; nothing changed."), 5000);
		return;
	}
	const int changed = static_cast<int>(report.changed.size());
	recordActivity(tr("Entity colour set"), report.value, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Set _color to %1 on %n entit(y)(ies) as one undo step.", nullptr, changed).arg(report.value);
	if (report.worldPrimitives > 0) {
		message += QLatin1Char(' ') + tr("%n world brush(es) or patch(es) have no entity colour and were left alone.", nullptr, report.worldPrimitives);
	}
	if (report.hidden > 0) {
		message += QLatin1Char(' ') + tr("%n hidden entit(y)(ies) kept their colour.", nullptr, report.hidden);
	}
	statusBar()->showMessage(message, 7000);
}

void ApplicationShell::normaliseLevelMapEntityColourFromUi()
{
	LevelEntityColourReport report;
	QString error;
	if (!normaliseLevelMapSelectionEntityColour(&m_levelMapDocument, LevelEntityColourNormalise::Primary, levelEntityColourKey(), &report, &error)) {
		statusBar()->showMessage(tr("Could not normalise the entity colour: %1").arg(error), 6000);
		refreshLevelMapWorkbench();
		return;
	}
	// NetRadiant Custom remembers the normalised colour for the next pick.
	LevelEntityColour written;
	if (parseLevelEntityColour(report.value, &written)) {
		m_settings.setLevelEntityColour(written.unit());
	}
	if (report.changed.isEmpty()) {
		statusBar()->showMessage(tr("The colour of entity %1 is already normalised; nothing changed.").arg(report.sourceEntity), 5000);
		return;
	}
	const int changed = static_cast<int>(report.changed.size());
	recordActivity(tr("Entity colour normalised"), report.value, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message =
		tr("Normalised the colour of entity %1 to %2 and set it on %n entit(y)(ies) as one undo step.", nullptr, changed).arg(report.sourceEntity).arg(report.value);
	if (report.worldPrimitives > 0) {
		message += QLatin1Char(' ') + tr("%n world brush(es) or patch(es) have no entity colour and were left alone.", nullptr, report.worldPrimitives);
	}
	if (report.hidden > 0) {
		message += QLatin1Char(' ') + tr("%n hidden entit(y)(ies) kept their colour.", nullptr, report.hidden);
	}
	statusBar()->showMessage(message, 7000);
}

} // namespace vibestudio
