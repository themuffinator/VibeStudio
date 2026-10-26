// NetRadiant Custom's Brush > Prism…, Cone… and Sphere… and its Make 3- to
// 9-Sided Prism keys: each selected brush, world or brush-entity, is rebuilt
// in place in its own bounds, round on its larger extent with a face on the
// first axis, along the active plan view's depth axis (a sphere keeps its
// poles on Z), in the current material, as one undo step
// (core/level_shapes.h rebuildLevelMapBrushesAsShape). In the NetRadiant
// Custom profile the views claim Ctrl+3 to Ctrl+9 for the sided prisms while
// a brush is selected; elsewhere those keys keep switching modes.
// Workflow reference: NRC radiant/brushmanip.cpp BrushPrefab, BrushMakeSided,
// DoSides and Scene_BrushConstructPrefab at 449778b3 (GPL-2.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_hollow.h"
#include "core/level_shapes.h"

#include <QAction>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QStatusBar>

#include <algorithm>

namespace vibestudio {

namespace {

// Spheres share their side count with their latitude bands, as NRC's do, so
// sides × sides stays within the component editor's 128 faces.
constexpr int kSphereMaxSides = 11;

int promptMaximum(const QString& shape)
{
	return shape == QLatin1String("sphere") ? kSphereMaxSides : levelBrushPrimitiveMaxSides(shape);
}

} // namespace

void ApplicationShell::registerLevelPrismCommands()
{
	struct Entry {
		QString commandId;
		QString label;
		QString tip;
		QString icon;
		QString shape;
		int sides = 0;
	};
	QVector<Entry> entries {
		{QStringLiteral("map.makePrism"), tr("Make Prism…"),
			tr("Rebuild each selected brush in place as a prism with the sides you choose, along the active view's depth axis, round on its larger extent with a face on the first axis, in the current material."),
			QStringLiteral("shape-cylinder"), QStringLiteral("cylinder")},
		{QStringLiteral("map.makeCone"), tr("Make Cone…"),
			tr("Rebuild each selected brush in place as a cone with the sides you choose, its base at the low end of the active view's depth axis and its point at the high end, in the current material."),
			QStringLiteral("shape-cone"), QStringLiteral("cone")},
		{QStringLiteral("map.makeSphere"), tr("Make Sphere…"),
			tr("Rebuild each selected brush in place as a faceted sphere on its largest extent, poles on Z, with as many sides and latitude bands as you choose, in the current material."),
			QStringLiteral("shape-sphere"), QStringLiteral("sphere")},
	};
	for (int sides = 3; sides <= 9; ++sides) {
		entries.push_back({levelSidedPrismCommandId(sides), tr("Make %1-Sided Prism").arg(sides),
			tr("Rebuild each selected brush in place as a %1-sided prism along the active view's depth axis, round on its larger extent with a face on the first axis, in the current material.")
				.arg(sides),
			QStringLiteral("polygon"), QStringLiteral("cylinder"), sides});
	}
	for (const Entry& entry : std::as_const(entries)) {
		StudioCommandRegistration command;
		command.commandId = entry.commandId;
		command.group = StudioCommandGroup::Edit;
		command.label = entry.label;
		command.statusTip = entry.tip;
		command.iconName = entry.icon;
		command.menuSection = tr("Brush Shapes");
		command.handler = [this, shape = entry.shape, sides = entry.sides]() {
			setMode(StudioMode::Levels);
			makeLevelPrismFromUi(shape, sides);
		};
		m_commands->registerCommand(command);
	}
}

void ApplicationShell::refreshLevelPrismCommands()
{
	if (!m_commands) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool brushes = quake && !levelCsgReachedBrushIds(m_levelMapDocument).isEmpty();
	for (const char* id : {"map.makePrism", "map.makeCone", "map.makeSphere"}) {
		m_commands->setEnabled(QString::fromLatin1(id), brushes);
	}
	for (int sides = 3; sides <= 9; ++sides) {
		m_commands->setEnabled(levelSidedPrismCommandId(sides), brushes);
	}
}

void ApplicationShell::makeLevelPrismFromUi(const QString& shape, int sides, QWidget* source)
{
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const QVector<int> brushes = quake ? levelCsgReachedBrushIds(m_levelMapDocument) : QVector<int> {};
	if (brushes.isEmpty()) {
		statusBar()->showMessage(tr("Select brushes, or a brush entity, in a Quake-family map to rebuild them as a shape."));
		return;
	}
	// The depth axis: the plan view the key came from, or the active one, as
	// NRC follows its current 2D view.
	int axis = levelShapeViewAxis();
	for (auto* view : std::as_const(m_levelPlanViews)) {
		if (view && view == source) {
			activateLevelPlanViewport(view);
			axis = levelShapeViewAxis();
		}
	}
	if (sides <= 0) {
		const QJsonObject saved = QJsonDocument::fromJson(m_settings.shellLayoutState(QStringLiteral("levelPrismSides"))).object();
		const int maximum = promptMaximum(shape);
		QInputDialog prompt(this);
		prompt.setObjectName(QStringLiteral("levelPrismSidesDialog"));
		prompt.setInputMode(QInputDialog::IntInput);
		prompt.setIntRange(3, maximum);
		prompt.setIntStep(1);
		prompt.setIntValue(std::clamp(saved.value(shape).toInt(8), 3, maximum));
		if (shape == QLatin1String("cone")) {
			prompt.setWindowTitle(tr("Make Cone"));
			prompt.setLabelText(tr("Sides around the cone (3 to %1):").arg(maximum));
		} else if (shape == QLatin1String("sphere")) {
			prompt.setWindowTitle(tr("Make Sphere"));
			prompt.setLabelText(tr("Sides and latitude bands of the sphere (3 to %1):").arg(maximum));
		} else {
			prompt.setWindowTitle(tr("Make Prism"));
			prompt.setLabelText(tr("Sides of the prism (3 to %1):").arg(maximum));
		}
		prompt.setAccessibleName(prompt.windowTitle());
		prompt.setAccessibleDescription(prompt.labelText());
		if (prompt.exec() != QDialog::Accepted) {
			return;
		}
		sides = prompt.intValue();
		QJsonObject updated = saved;
		updated.insert(shape, sides);
		m_settings.setShellLayoutState(QStringLiteral("levelPrismSides"), QJsonDocument(updated).toJson(QJsonDocument::Compact));
		// The map may have changed while the prompt was open.
		if (!quake || levelCsgReachedBrushIds(m_levelMapDocument).isEmpty()) {
			statusBar()->showMessage(tr("Select brushes, or a brush entity, in a Quake-family map to rebuild them as a shape."));
			return;
		}
	}
	LevelShapeRequest request;
	request.shape = shape;
	request.axis = shape == QLatin1String("sphere") ? 2 : axis;
	request.sides = sides;
	request.bands = shape == QLatin1String("sphere") ? sides : request.bands;
	request.fit = LevelBrushPrimitiveFit::Circle;
	request.texture = levelBrushMaterial();
	LevelShapeRebuildReport report;
	QString error;
	if (!rebuildLevelMapBrushesAsShape(&m_levelMapDocument, levelCsgReachedBrushIds(m_levelMapDocument), request, &report, &error)) {
		statusBar()->showMessage(tr("Could not rebuild the brushes: %1").arg(error), 6000);
		return;
	}
	rememberLevelMaterial(request.texture);
	const int count = static_cast<int>(report.brushIds.size());
	const QString axisName(QLatin1Char("XYZ"[request.axis]));
	QString message;
	if (shape == QLatin1String("cone")) {
		message = tr("Rebuilt %n brush(es) as %1-sided cones along %2 in %3.", nullptr, count).arg(sides).arg(axisName, request.texture);
	} else if (shape == QLatin1String("sphere")) {
		message = tr("Rebuilt %n brush(es) as spheres of %1 sides and bands in %2.", nullptr, count).arg(sides).arg(request.texture);
	} else {
		message = tr("Rebuilt %n brush(es) as %1-sided prisms along %2 in %3.", nullptr, count).arg(sides).arg(axisName, request.texture);
	}
	recordActivity(tr("Brushes rebuilt as a shape"), QStringLiteral("%1 %2").arg(shape).arg(sides), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(message + levelMapSkippedNote(report.skipped), 6000);
}

bool ApplicationShell::handleLevelPrismKey(QObject* watched, QEvent* event)
{
	if ((event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) || !m_commands) {
		return false;
	}
	MapViewport* plan = nullptr;
	for (auto* view : std::as_const(m_levelPlanViews)) {
		if (view && view == watched) {
			plan = view;
		}
	}
	const bool camera = m_levelMap3D && watched == m_levelMap3D;
	if (!plan && !camera) {
		return false;
	}
	auto* key = static_cast<QKeyEvent*>(event);
	const QKeyCombination pressed(key->modifiers() & ~Qt::KeypadModifier, Qt::Key(key->key()));
	int sides = 0;
	for (int candidate = 3; candidate <= 9 && sides == 0; ++candidate) {
		for (const QKeySequence& sequence : m_commands->shortcutSequences(levelSidedPrismCommandId(candidate))) {
			if (sequence.count() == 1 && sequence[0] == pressed) {
				sides = candidate;
			}
		}
	}
	if (sides == 0) {
		return false;
	}
	if (event->type() == QEvent::ShortcutOverride) {
		// Read fresh, so the claim never lags a selection change.
		refreshLevelPrismCommands();
	}
	const QAction* action = m_commands->action(levelSidedPrismCommandId(sides));
	if (!action || !action->isEnabled()) {
		// Nothing to rebuild: the key keeps its window-wide meaning.
		return false;
	}
	event->accept();
	if (event->type() == QEvent::ShortcutOverride || key->isAutoRepeat()) {
		return true;
	}
	// The view's own keys and gestures come first.
	if ((plan && plan->keepsKey(key->key(), key->modifiers())) || (camera && m_levelMap3D->isMovingComponents())) {
		return false;
	}
	makeLevelPrismFromUi(QStringLiteral("cylinder"), sides, plan ? static_cast<QWidget*>(plan) : static_cast<QWidget*>(m_levelMap3D));
	return true;
}

} // namespace vibestudio
