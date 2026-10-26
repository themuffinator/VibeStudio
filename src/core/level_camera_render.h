#pragma once

// How the Levels camera draws the map, shared by the View > Camera Display
// commands, the View tab, the status-strip Textures box and the tests.
//
// Three saved choices make up the picture: whether faces show package images
// (Textures in Camera) or flat colours, whether faces are drawn at all (3D
// Wireframe hides them and leaves the edges), and whether brush polygon edges
// are drawn over the faces (Edges in Camera). TrenchBroom's View Filter sets
// the face mode outright (Show Textures, Hide Textures, Hide Faces) and
// toggles the edges; NetRadiant Custom's Next and Previous Camera Mode step
// through Wireframe, Flat Shaded, Textured and Textured with Edges, wrapping
// at either end. Hiding faces keeps the textures and edges choices, so
// turning 3D Wireframe off brings back the look it replaced.
//
// Workflow reference: TB MapViewBase::showMaterials, hideMaterials, hideFaces
// and toggleShowEdges at 4c756be1 (GPL-3.0-or-later) / NRC camwindow.cpp
// CameraModeNext and CameraModePrev at 449778b3 (GPL-2.0-or-later). Original
// Qt implementation; see docs/CREDITS.md.

#include <QString>

namespace vibestudio {

// What the camera does with brush, patch and placed-model faces.
enum class LevelCameraFaceMode {
	// Package images and static shader previews.
	Textured,
	// Flat-shaded colours, no images.
	Flat,
	// No faces: the polygon edges alone, as 3D Wireframe draws them.
	Hidden,
};

// NetRadiant Custom's camera draw modes, in the order its cycle walks them.
enum class LevelCameraDrawMode {
	Wireframe,
	Flat,
	Textured,
	TexturedEdges,
};

inline constexpr int kLevelCameraDrawModeCount = 4;

// The saved camera display choices. The defaults are the camera as it has
// always looked: textured faces, no edge overlay.
struct LevelCameraRenderState {
	bool textures = true;
	bool facesHidden = false;
	bool edges = false;

	friend bool operator==(const LevelCameraRenderState&, const LevelCameraRenderState&) = default;
};

[[nodiscard]] LevelCameraFaceMode levelCameraFaceMode(const LevelCameraRenderState& state);
// The state with `mode` as its face mode. Textured and Flat show the faces
// with or without images; Hidden hides them and keeps the textures choice for
// when they come back. The edges choice is always kept.
[[nodiscard]] LevelCameraRenderState levelCameraWithFaceMode(LevelCameraRenderState state, LevelCameraFaceMode mode);
// Whether the camera outlines the faces it draws: only with Edges in Camera
// on and the faces shown (hidden faces leave the edges as the whole picture).
[[nodiscard]] bool levelCameraDrawsEdgeOverlay(const LevelCameraRenderState& state);

// Where `state` sits in the cycle: hidden faces are Wireframe, flat faces are
// Flat with or without edges, and textured faces are Textured or Textured
// with Edges.
[[nodiscard]] LevelCameraDrawMode levelCameraDrawMode(const LevelCameraRenderState& state);
// The state that draws `mode`. Wireframe only hides the faces, keeping the
// other choices; the rest set the faces, images and edges exactly.
[[nodiscard]] LevelCameraRenderState levelCameraWithDrawMode(LevelCameraRenderState state, LevelCameraDrawMode mode);
// `steps` modes forward (positive) or back (negative) from `mode`, wrapping
// at either end of the cycle.
[[nodiscard]] LevelCameraDrawMode levelCameraSteppedDrawMode(LevelCameraDrawMode mode, int steps);
// Position in the cycle, counted from 1 for display.
[[nodiscard]] int levelCameraDrawModeNumber(LevelCameraDrawMode mode);

// Stable identifiers: "wireframe", "flat", "textured", "textured-edges".
[[nodiscard]] QString levelCameraDrawModeId(LevelCameraDrawMode mode);
[[nodiscard]] bool levelCameraDrawModeFromId(const QString& id, LevelCameraDrawMode* mode);
// Translated names, such as "Textured with Edges".
[[nodiscard]] QString levelCameraDrawModeName(LevelCameraDrawMode mode);
// A translated sentence describing what the camera now draws, such as
// "Camera: flat-shaded faces with edges."
[[nodiscard]] QString levelCameraRenderSummary(const LevelCameraRenderState& state);
// A translated sentence for a step through the cycle, such as
// "Camera mode 3 of 4: Textured."
[[nodiscard]] QString levelCameraDrawModeAnnouncement(LevelCameraDrawMode mode);

} // namespace vibestudio
