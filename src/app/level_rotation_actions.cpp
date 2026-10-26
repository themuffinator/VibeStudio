// Rotate Selection… and Scale Selection…, NetRadiant Custom's Arbitrary
// Rotation (Shift+R) and Arbitrary Scale (Ctrl+Shift+S) in its profile. Both
// windows are modeless: Apply commits through the shared placement services
// and keeps the window open, and every refresh of the Levels commands hands
// them the current map, so the next Apply and its preview use the refreshed
// selection. In component mode they preview and transform the picked
// components instead, through the Affine and Scale component transforms.
// Workflow reference: NRC radiant/select.cpp RotateDialog, ScaleDialog,
// DoRotateDlg and DoScaleDlg at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_rotation_dialog.h"
#include "app/level_scale_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "core/level_brush.h"
#include "core/level_placement.h"
#include "core/level_texture_mapping.h"
#include <QPointer>
#include <QStatusBar>

namespace vibestudio {
namespace {
const char* const kLoadSerial = "levelLoadSerial";
const char* const kComponents = "levelComponents";

// The open Rotate or Scale window, if any. Neither class has its own meta
// object, so they are found by name; a closed one awaits deletion, hidden.
QDialog* openTransformDialog(QWidget* shell, const QString& name)
{
	for (auto* dialog : shell->findChildren<QDialog*>(name, Qt::FindDirectChildrenOnly)) {
		if (dialog->isVisible()) {
			return dialog;
		}
	}
	return nullptr;
}

bool showOpenDialog(QDialog* dialog)
{
	if (!dialog) {
		return false;
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
	return true;
}

// Whether the window previewed this map, selection and component picks. The
// content revision covers edits, undo and redo; selection-only history steps
// change nothing the preview shows.
bool previewsCurrent(const LevelTransformDialog& dialog, quint64 load, const LevelMapDocument& document, const QString& components)
{
	const auto& source = dialog.sourceDocument();
	return dialog.property(kLoadSerial).toULongLong() == load && source.revision == document.revision
		&& source.selection == document.selection && dialog.property(kComponents).toString() == components;
}

// The object request as a transform of the picked components, with the
// component UV settings and pivot that applyLevelComponentTransform() uses.
LevelPlacementRequest componentPlacement(const LevelPlacementRequest& objects, const QVector<LevelMapComponentSet>& picked,
	const LevelSurfaceFacePicks& faces, const LevelComponentTransformRequest& base, const LevelMapVec3& componentPivot)
{
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::TransformComponents;
	work.components = picked;
	work.surfaceFaces = faces;
	auto transform = base;
	if (objects.operation == LevelPlacementOperation::Rotate) {
		transform.operation = LevelComponentTransformKind::Affine;
		for (auto& column : transform.linear) {
			for (const auto& step : levelMapRotationSteps(objects.rotation)) {
				column = rotateLevelVector(column, step.first, step.second);
			}
		}
		transform.delta = {0, 0, 0, true};
		transform.pivot = objects.rotation.pivot;
	} else {
		transform.operation = LevelComponentTransformKind::Scale;
		transform.scale = {objects.scale.factors[0], objects.scale.factors[1], objects.scale.factors[2], true};
		transform.pivot = objects.scale.pivot;
	}
	if (!transform.pivot.valid) {
		transform.pivot = componentPivot;
	}
	work.componentTransform = transform;
	return work;
}
} // namespace

bool ApplicationShell::applyLevelRotation(const LevelMapRotationRequest& request, QString* error) {
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Rotate;
	work.rotation = request;
	if (!runLevelPlacementFromUi(work, error)) {
		return false;
	}
	recordActivity(tr("Level selection rotated"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Selection rotated. Save the map to include it in the next build."));
	return true;
}

bool ApplicationShell::applyLevelScale(const LevelMapScaleRequest& request, QString* error) {
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Scale;
	work.scale = request;
	if (!runLevelPlacementFromUi(work, error)) {
		return false;
	}
	recordActivity(tr("Level selection scaled"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Selection scaled. Save the map to include it in the next build."));
	return true;
}

void ApplicationShell::rotateLevelMapSelectionPreciselyFromUi() {
	if (showOpenDialog(openTransformDialog(this, QStringLiteral("levelRotationDialog")))) {
		return;
	}
	if (m_levelMapDocument.selection.isEmpty() && !levelComponentsActive()) {
		return;
	}
	int axis = 2;
	if (m_levelMapViewport && m_levelMapDocument.format != LevelMapFormat::DoomWad) {
		if (m_levelMapViewport->projection() == MapViewportProjection::FrontXZ) {
			axis = 1;
		}
		if (m_levelMapViewport->projection() == MapViewportProjection::SideZY) {
			axis = 0;
		}
	}
	auto* dialog = new LevelRotationDialog(m_levelMapDocument, axis, this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	// Unset, so the first refresh below adopts the current state.
	dialog->setProperty(kLoadSerial, QVariant::fromValue(m_levelMapLoadSerial + 1));
	auto initial = dialog->request();
	initial.textureLock = m_settings.levelTextureLock();
	initial.allowValve220 = m_settings.levelAllowValve220();
	dialog->setRequest(initial);
	const QPointer<LevelRotationDialog> guard(dialog);
	dialog->setApplyHandler([this, guard](const LevelMapRotationRequest& request, QString* error) {
		if (!guard || !previewsCurrent(*guard, m_levelMapLoadSerial, m_levelMapDocument, levelComponentsActive() ? levelComponentSelectionToken() : QString())) {
			if (error) {
				*error = tr("The map or selection changed after this preview. It is being rebuilt; press Apply again when it is ready.");
			}
			refreshLevelArbitraryTransformDialogs();
			return false;
		}
		return applyLevelRotation(request, error);
	});
	// Shown first: the refresh adopts visible windows only.
	dialog->show();
	refreshLevelArbitraryTransformDialogs();
}

void ApplicationShell::scaleLevelMapSelectionFromUi() {
	if (showOpenDialog(openTransformDialog(this, QStringLiteral("levelScaleDialog")))) {
		return;
	}
	if (m_levelMapDocument.selection.isEmpty() && !levelComponentsActive()) {
		return;
	}
	auto* dialog = new LevelScaleDialog(m_levelMapDocument, this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setProperty(kLoadSerial, QVariant::fromValue(m_levelMapLoadSerial + 1));
	auto initial = dialog->request();
	initial.textures = {m_settings.levelTextureScaleLock(), m_settings.levelAllowValve220()};
	dialog->setRequest(initial);
	const QPointer<LevelScaleDialog> guard(dialog);
	dialog->setApplyHandler([this, guard](const LevelMapScaleRequest& request, QString* error) {
		if (!guard || !previewsCurrent(*guard, m_levelMapLoadSerial, m_levelMapDocument, levelComponentsActive() ? levelComponentSelectionToken() : QString())) {
			if (error) {
				*error = tr("The map or selection changed after this preview. It is being rebuilt; press Apply again when it is ready.");
			}
			refreshLevelArbitraryTransformDialogs();
			return false;
		}
		return applyLevelScale(request, error);
	});
	// Shown first: the refresh adopts visible windows only.
	dialog->show();
	refreshLevelArbitraryTransformDialogs();
}

void ApplicationShell::registerLevelArbitraryTransformCommands()
{
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.scaleSelection");
	command.group = StudioCommandGroup::Edit;
	command.label = tr("Scale Selection…");
	command.statusTip = tr("Scale the selection, or the picked components, by X, Y and Z factors about a pivot; a negative factor mirrors. The window stays open for repeated Apply.");
	command.iconName = QStringLiteral("resize");
	command.handler = [this]() {
		setMode(StudioMode::Levels);
		scaleLevelMapSelectionFromUi();
	};
	m_commands->registerCommand(command);
}

void ApplicationShell::refreshLevelArbitraryTransformDialogs()
{
	const bool components = levelComponentsActive();
	if (m_commands) {
		m_commands->setEnabled(QStringLiteral("map.scaleSelection"),
			m_levelMapDocument.format != LevelMapFormat::Unknown && (!m_levelMapDocument.selection.isEmpty() || components));
	}
	QString token;
	bool tokenRead = false;
	for (const QString& name : {QStringLiteral("levelRotationDialog"), QStringLiteral("levelScaleDialog")}) {
		auto* dialog = static_cast<LevelTransformDialog*>(openTransformDialog(this, name));
		if (!dialog) {
			continue;
		}
		if (!tokenRead) {
			token = components ? levelComponentSelectionToken() : QString();
			tokenRead = true;
		}
		if (previewsCurrent(*dialog, m_levelMapLoadSerial, m_levelMapDocument, token)) {
			continue;
		}
		dialog->setProperty(kLoadSerial, QVariant::fromValue(m_levelMapLoadSerial));
		dialog->setProperty(kComponents, token);
		if (components) {
			// Picks taken now; any later change refreshes the window again.
			const auto picked = m_levelMapViewport->pickedComponents();
			const auto faces = levelComponentFacePicks();
			LevelComponentTransformRequest base;
			applyLevelComponentUvSettings(&base);
			const auto pivot = m_levelComponentPivot;
			dialog->setPlacementAdapter([picked, faces, base, pivot](const LevelPlacementRequest& objects) {
				return componentPlacement(objects, picked, faces, base, pivot);
			});
			dialog->setBlockedReason(picked.isEmpty() && faces.faces.isEmpty()
				? tr("Pick vertices, edges or faces to transform, or leave component mode to transform whole objects.")
				: QString());
			dialog->setStatusNote(tr("Applies to the picked components, with Lock Component UVs and your component pivot."));
		} else {
			dialog->setPlacementAdapter({});
			dialog->setBlockedReason({});
			dialog->setStatusNote({});
		}
		dialog->setSource(m_levelMapDocument);
	}
}
} // namespace vibestudio
