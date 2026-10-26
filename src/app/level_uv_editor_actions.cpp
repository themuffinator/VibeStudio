// The UV Editor command: one picked brush face, or one selected patch, in
// texture space (app/level_uv_editor_dialog.h), committed as one undo step
// through the surface service that `map edit-uv` shares
// (core/level_uv_edit.h). Workflow reference: NetRadiant Custom's TexTool
// plugin, Plugins > TexTool > Go... (plugins/textool/TexTool.cpp at
// 449778b3, GPL-2.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_uv_editor_dialog.h"
#include "app/studio_actions.h"
#include "core/editor_profiles.h"
#include "core/level_uv_edit.h"

#include <QStatusBar>

namespace vibestudio {

void ApplicationShell::registerLevelUvEditorCommands()
{
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.uvEditor");
	command.label = tr("UV Editor…");
	command.group = StudioCommandGroup::Edit;
	command.menuSection = tr("Surface Adjustments");
	command.iconName = QStringLiteral("image");
	command.statusTip = tr("Drag the picked face's or the selected patch's points over its material image: move the texture, turn it about an "
						   "anchor point, zoom, pan and nudge, with a live preview. Apply makes it one undo step.");
	command.handler = [this] { openLevelUvEditorFromUi(); };
	m_commands->registerCommand(command);
	m_commands->setEnabled(command.commandId, false);
}

bool ApplicationShell::levelUvEditorTarget(LevelMaterialTarget* target, QString* reason) const
{
	const auto refuse = [reason](const QString& message) {
		if (reason) {
			*reason = message;
		}
		return false;
	};
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		return refuse(tr("Open a Quake, Quake II or Quake III map to edit texture coordinates."));
	}
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy()) {
		return refuse(tr("Finish the current surface or geometry edit before opening the UV Editor."));
	}
	// One picked face (or the Inspector's face of the selected brush), or
	// else one selected patch.
	LevelMaterialTarget found;
	const QVector<LevelSurfaceFace> faces = levelSurfaceFaceTargets();
	if (faces.size() == 1) {
		found = {LevelMaterialKind::BrushFace, faces.first().brushId, faces.first().faceIndex};
	} else if (faces.isEmpty() && m_levelMapDocument.selection.size() == 1
		&& m_levelMapDocument.selection.first().kind == LevelMapSelectionKind::QuakePatch) {
		found = {LevelMaterialKind::Patch, m_levelMapDocument.selection.first().objectId, 0};
	} else if (faces.size() > 1) {
		return refuse(tr("Pick just one brush face for the UV Editor."));
	} else {
		return refuse(tr("Pick one brush face, or select one patch, for the UV Editor."));
	}
	if (const QString problem = levelUvTargetProblem(m_levelMapDocument, found); !problem.isEmpty()) {
		return refuse(problem);
	}
	if (target) {
		*target = found;
	}
	return true;
}

void ApplicationShell::refreshLevelUvEditorCommand()
{
	if (!m_commands) {
		return;
	}
	m_commands->setEnabled(QStringLiteral("map.uvEditor"), levelUvEditorTarget());
}

bool ApplicationShell::applyLevelUvEdit(const LevelSurfaceEditPlan& plan, QString* error)
{
	const quint64 revision = m_levelMapDocument.revision;
	if (!commitLevelSurfaceEdit(&m_levelMapDocument, plan, error)) {
		return false;
	}
	if (revision != m_levelMapDocument.revision) {
		recordActivity(tr("Texture coordinates edited"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
			tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Texture coordinates updated in one undo step. Save the map to include them in the next build."), 5000);
	}
	return true;
}

void ApplicationShell::openLevelUvEditorFromUi()
{
	LevelMaterialTarget target;
	QString reason;
	if (!levelUvEditorTarget(&target, &reason)) {
		statusBar()->showMessage(reason, 6000);
		return;
	}
	// The image the Levels preview already read, if it did.
	QString material;
	if (target.kind == LevelMaterialKind::BrushFace) {
		for (const LevelMapBrush& brush : std::as_const(m_levelMapDocument.brushes)) {
			if (brush.id == target.objectId) {
				material = brush.faces.value(target.faceIndex).textureName;
			}
		}
	} else {
		for (const LevelMapPatch& patch : std::as_const(m_levelMapDocument.patches)) {
			if (patch.id == target.objectId) {
				material = patch.textureName;
			}
		}
	}
	const QString key = material.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded();
	const LevelPreviewMaterial* cached = nullptr;
	for (const LevelPreviewMaterial& candidate : std::as_const(m_levelPreviewAssets.materials)) {
		if (candidate.key == key && candidate.ready()) {
			cached = &candidate;
		}
	}
	auto archive = m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelUvEditorDialog dialog(m_levelMapDocument, target, archive, activePaletteId(), cached, this);
	const auto controls = m_levelControlsApplied ? m_levelControls : levelEditorControlsForProfile(m_settings.selectedEditorProfileId());
	dialog.setTurnStep(controls.surfaceRotationStep);
	const auto load = m_levelMapLoadSerial;
	const auto revision = m_levelMapDocument.revision;
	const QString packageKey = packageViewKey();
	const auto palette = activePaletteId();
	dialog.setApplyHandler([this, load, revision, packageKey, palette](const LevelSurfaceEditPlan& plan, QString* error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || packageKey != packageViewKey()
			|| palette != activePaletteId() || levelSurfaceEditsPending() || levelSurfaceGeometryBusy()) {
			if (error) {
				*error = tr("The map or package changed while the UV Editor was open. Cancel and open the UV Editor again.");
			}
			return false;
		}
		return applyLevelUvEdit(plan, error);
	});
	dialog.exec();
}

} // namespace vibestudio
