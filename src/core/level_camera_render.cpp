// How the Levels camera draws the map (core/level_camera_render.h).
//
// Workflow reference: TB MapViewBase::showMaterials, hideMaterials, hideFaces
// and toggleShowEdges at 4c756be1 (GPL-3.0-or-later) / NRC camwindow.cpp
// CameraModeNext and CameraModePrev at 449778b3 (GPL-2.0-or-later). Original
// Qt implementation; see docs/CREDITS.md.

#include "core/level_camera_render.h"

#include <QCoreApplication>

namespace vibestudio {

LevelCameraFaceMode levelCameraFaceMode(const LevelCameraRenderState& state)
{
	if (state.facesHidden) {
		return LevelCameraFaceMode::Hidden;
	}
	return state.textures ? LevelCameraFaceMode::Textured : LevelCameraFaceMode::Flat;
}

LevelCameraRenderState levelCameraWithFaceMode(LevelCameraRenderState state, LevelCameraFaceMode mode)
{
	switch (mode) {
	case LevelCameraFaceMode::Textured:
		state.facesHidden = false;
		state.textures = true;
		break;
	case LevelCameraFaceMode::Flat:
		state.facesHidden = false;
		state.textures = false;
		break;
	case LevelCameraFaceMode::Hidden:
		state.facesHidden = true;
		break;
	}
	return state;
}

bool levelCameraDrawsEdgeOverlay(const LevelCameraRenderState& state)
{
	return state.edges && !state.facesHidden;
}

LevelCameraDrawMode levelCameraDrawMode(const LevelCameraRenderState& state)
{
	if (state.facesHidden) {
		return LevelCameraDrawMode::Wireframe;
	}
	if (!state.textures) {
		return LevelCameraDrawMode::Flat;
	}
	return state.edges ? LevelCameraDrawMode::TexturedEdges : LevelCameraDrawMode::Textured;
}

LevelCameraRenderState levelCameraWithDrawMode(LevelCameraRenderState state, LevelCameraDrawMode mode)
{
	switch (mode) {
	case LevelCameraDrawMode::Wireframe:
		state.facesHidden = true;
		break;
	case LevelCameraDrawMode::Flat:
		state = {false, false, false};
		break;
	case LevelCameraDrawMode::Textured:
		state = {true, false, false};
		break;
	case LevelCameraDrawMode::TexturedEdges:
		state = {true, false, true};
		break;
	}
	return state;
}

LevelCameraDrawMode levelCameraSteppedDrawMode(LevelCameraDrawMode mode, int steps)
{
	const int count = kLevelCameraDrawModeCount;
	const int index = ((static_cast<int>(mode) + steps % count) % count + count) % count;
	return static_cast<LevelCameraDrawMode>(index);
}

int levelCameraDrawModeNumber(LevelCameraDrawMode mode)
{
	return static_cast<int>(mode) + 1;
}

QString levelCameraDrawModeId(LevelCameraDrawMode mode)
{
	switch (mode) {
	case LevelCameraDrawMode::Wireframe:
		return QStringLiteral("wireframe");
	case LevelCameraDrawMode::Flat:
		return QStringLiteral("flat");
	case LevelCameraDrawMode::Textured:
		return QStringLiteral("textured");
	case LevelCameraDrawMode::TexturedEdges:
		return QStringLiteral("textured-edges");
	}
	return {};
}

bool levelCameraDrawModeFromId(const QString& id, LevelCameraDrawMode* mode)
{
	const QString wanted = id.trimmed().toLower();
	for (int index = 0; index < kLevelCameraDrawModeCount; ++index) {
		const auto candidate = static_cast<LevelCameraDrawMode>(index);
		if (levelCameraDrawModeId(candidate) == wanted) {
			if (mode) {
				*mode = candidate;
			}
			return true;
		}
	}
	return false;
}

QString levelCameraDrawModeName(LevelCameraDrawMode mode)
{
	switch (mode) {
	case LevelCameraDrawMode::Wireframe:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Wireframe");
	case LevelCameraDrawMode::Flat:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Flat Shaded");
	case LevelCameraDrawMode::Textured:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Textured");
	case LevelCameraDrawMode::TexturedEdges:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Textured with Edges");
	}
	return {};
}

QString levelCameraRenderSummary(const LevelCameraRenderState& state)
{
	switch (levelCameraFaceMode(state)) {
	case LevelCameraFaceMode::Hidden:
		return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: wireframe, faces hidden.");
	case LevelCameraFaceMode::Flat:
		return state.edges ? QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: flat-shaded faces with edges.")
						   : QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: flat-shaded faces.");
	case LevelCameraFaceMode::Textured:
		break;
	}
	return state.edges ? QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: textured faces with edges.")
					   : QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera: textured faces.");
}

QString levelCameraDrawModeAnnouncement(LevelCameraDrawMode mode)
{
	return QCoreApplication::translate("VibeStudioLevelCameraRender", "Camera mode %1 of %2: %3.")
		.arg(levelCameraDrawModeNumber(mode))
		.arg(kLevelCameraDrawModeCount)
		.arg(levelCameraDrawModeName(mode));
}

} // namespace vibestudio
