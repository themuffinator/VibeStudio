// View > Camera Display: how the Levels camera draws the map. Textures in
// Camera and Edges in Camera are saved choices, also on the View tab (and the
// Textures box above the views); 3D Wireframe hides the faces and keeps those
// choices for when it is turned off. Show Textures in Camera, Hide Textures in
// Camera and Hide Faces in Camera set the face mode outright, as TrenchBroom's
// View Filter does, and Next and Previous Camera Mode step NetRadiant Custom's
// four-mode cycle, saying in the status bar which mode is now drawn. They
// change only the picture: the map, its undo history, the selection, scene
// locks, hidden objects and the open group are untouched.
//
// Workflow reference: TB MapViewBase::showMaterials, hideMaterials, hideFaces
// and toggleShowEdges at 4c756be1 (GPL-3.0-or-later) / NRC camwindow.cpp
// CameraModeNext and CameraModePrev at 449778b3 (GPL-2.0-or-later). Original
// Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_camera_render.h"

#include <QAction>
#include <QStatusBar>

#include <functional>
#include <utility>

namespace vibestudio {

namespace {

const QString kTexturedKey = QStringLiteral("levelPreviewTextured");
const QString kWireframeKey = QStringLiteral("levelPreviewWireframe");
const QString kEdgesKey = QStringLiteral("levelPreviewEdges");

QByteArray onOff(bool on)
{
	return on ? QByteArrayLiteral("on") : QByteArrayLiteral("off");
}

} // namespace

void ApplicationShell::registerLevelCameraRenderCommands()
{
	if (!m_commands) {
		return;
	}
	// Textures were on unless turned off; faces hidden and edges only when
	// they were turned on.
	m_levelCameraRender.textures = m_settings.shellLayoutState(kTexturedKey) != QByteArrayLiteral("off");
	m_levelCameraRender.facesHidden = m_settings.shellLayoutState(kWireframeKey) == QByteArrayLiteral("on");
	m_levelCameraRender.edges = m_settings.shellLayoutState(kEdgesKey) == QByteArrayLiteral("on");

	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, bool checkable,
		std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.checkable = checkable;
		command.menuSection = tr("Camera Display");
		command.handler = std::move(handler);
		return m_commands->registerCommand(command);
	};
	m_levelPreviewTextured = add(QStringLiteral("map.previewTextures"), tr("Textures in Camera"),
		tr("Draw the camera's faces with package images and static shader previews; untick for flat-shaded colours. The choice is kept."),
		QStringLiteral("image"), true, [this]() {
			LevelCameraRenderState state = m_levelCameraRender;
			state.textures = !state.textures;
			QString message = levelCameraRenderSummary(state);
			if (state.facesHidden) {
				message = state.textures ? tr("Textures in Camera on: the faces show their textures when 3D Wireframe is turned off.")
										 : tr("Textures in Camera off: the faces are flat-shaded when 3D Wireframe is turned off.");
			}
			setLevelCameraRender(state, message);
		});
	add(QStringLiteral("map.previewEdges"), tr("Edges in Camera"),
		tr("Outline every brush face and patch quad over the camera's faces, without the triangles they are drawn with. The choice is kept."),
		QStringLiteral("grid"), true, [this]() {
			LevelCameraRenderState state = m_levelCameraRender;
			state.edges = !state.edges;
			QString message = levelCameraRenderSummary(state);
			if (state.facesHidden) {
				message = state.edges ? tr("Edges in Camera on: edges are drawn over the faces when 3D Wireframe is turned off.")
									  : tr("Edges in Camera off: the faces are drawn without edges when 3D Wireframe is turned off.");
			}
			setLevelCameraRender(state, message);
		});
	add(QStringLiteral("map.showCameraTextures"), tr("Show Textures in Camera"),
		tr("Draw the camera's faces textured, turning 3D Wireframe off if it is on."), QStringLiteral("image"), false, [this]() {
			const LevelCameraRenderState state = levelCameraWithFaceMode(m_levelCameraRender, LevelCameraFaceMode::Textured);
			setLevelCameraRender(state, levelCameraRenderSummary(state));
		});
	add(QStringLiteral("map.hideCameraTextures"), tr("Hide Textures in Camera"),
		tr("Draw the camera's faces in flat-shaded colours, turning 3D Wireframe off if it is on."), QStringLiteral("cube"), false, [this]() {
			const LevelCameraRenderState state = levelCameraWithFaceMode(m_levelCameraRender, LevelCameraFaceMode::Flat);
			setLevelCameraRender(state, levelCameraRenderSummary(state));
		});
	add(QStringLiteral("map.hideCameraFaces"), tr("Hide Faces in Camera"),
		tr("Draw no faces in the camera, only the brush and patch edges, as 3D Wireframe does."), QStringLiteral("cube"), false, [this]() {
			const LevelCameraRenderState state = levelCameraWithFaceMode(m_levelCameraRender, LevelCameraFaceMode::Hidden);
			setLevelCameraRender(state, levelCameraRenderSummary(state));
		});
	add(QStringLiteral("map.nextCameraMode"), tr("Next Camera Mode"),
		tr("Step the camera to the next of Wireframe, Flat Shaded, Textured and Textured with Edges, after the last back to the first."),
		QStringLiteral("cube"), false, [this]() { stepLevelCameraDrawMode(1); });
	add(QStringLiteral("map.previousCameraMode"), tr("Previous Camera Mode"),
		tr("Step the camera back through Textured with Edges, Textured, Flat Shaded and Wireframe, before the first round to the last."),
		QStringLiteral("cube"), false, [this]() { stepLevelCameraDrawMode(-1); });
	refreshLevelCameraRenderCommands();
}

void ApplicationShell::refreshLevelCameraRenderCommands()
{
	if (!m_commands) {
		return;
	}
	// The saved choices can change at any time; what changes the picture at
	// once waits for the camera to be showing, as 3D Wireframe always has.
	const bool showing = m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMap3D && levelMap3DShowing();
	for (const QString& id : {QStringLiteral("map.previewWireframe"), QStringLiteral("map.showCameraTextures"), QStringLiteral("map.hideCameraTextures"),
			 QStringLiteral("map.hideCameraFaces"), QStringLiteral("map.nextCameraMode"), QStringLiteral("map.previousCameraMode")}) {
		m_commands->setEnabled(id, showing);
	}
	m_commands->setEnabled(QStringLiteral("map.previewTextures"), true);
	m_commands->setEnabled(QStringLiteral("map.previewEdges"), true);
	const std::pair<const char*, bool> checks[] = {
		{"map.previewTextures", m_levelCameraRender.textures},
		{"map.previewEdges", m_levelCameraRender.edges},
		{"map.previewWireframe", m_levelCameraRender.facesHidden},
	};
	for (const auto& [id, checked] : checks) {
		if (QAction* action = m_commands->action(QString::fromLatin1(id)); action && action->isChecked() != checked) {
			action->setChecked(checked);
		}
	}
}

void ApplicationShell::applyLevelCameraRender()
{
	if (!m_levelMap3D) {
		return;
	}
	// Brush faces and patch quads are polygon fans in the preview mesh, so
	// their outlines leave the triangle diagonals out.
	m_levelMap3D->setPolygonEdges(true);
	m_levelMap3D->setShowEdges(levelCameraDrawsEdgeOverlay(m_levelCameraRender));
	switch (levelCameraFaceMode(m_levelCameraRender)) {
	case LevelCameraFaceMode::Hidden:
		m_levelMap3D->setRenderMode(ModelViewportRenderMode::Wireframe);
		break;
	case LevelCameraFaceMode::Flat:
		m_levelMap3D->setRenderMode(ModelViewportRenderMode::FlatShaded);
		break;
	case LevelCameraFaceMode::Textured:
		m_levelMap3D->setRenderMode(ModelViewportRenderMode::Textured);
		break;
	}
}

void ApplicationShell::setLevelCameraRender(const LevelCameraRenderState& state, const QString& message)
{
	m_levelCameraRender = state;
	m_settings.setShellLayoutState(kTexturedKey, onOff(state.textures));
	m_settings.setShellLayoutState(kWireframeKey, onOff(state.facesHidden));
	m_settings.setShellLayoutState(kEdgesKey, onOff(state.edges));
	applyLevelCameraRender();
	refreshLevelCameraRenderCommands();
	if (!message.isEmpty()) {
		statusBar()->showMessage(message, 5000);
	}
}

void ApplicationShell::stepLevelCameraDrawMode(int steps)
{
	const LevelCameraDrawMode mode = levelCameraSteppedDrawMode(levelCameraDrawMode(m_levelCameraRender), steps);
	setLevelCameraRender(levelCameraWithDrawMode(m_levelCameraRender, mode), levelCameraDrawModeAnnouncement(mode));
}

} // namespace vibestudio
