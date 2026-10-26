// The Levels camera display model (core/level_camera_render.h): the face
// modes TrenchBroom's View Filter sets, 3D Wireframe keeping the textures and
// edges choices, the edge overlay rule, NetRadiant Custom's four-mode cycle in
// both directions with wrapping, stable identifiers and the translated names
// and announcements.

#include "core/level_camera_render.h"

#include <QCoreApplication>
#include <QStringList>

#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toUtf8().constData() << std::endl;
	}
	return value;
}

QString describe(const LevelCameraRenderState& state)
{
	return QStringLiteral("textures=%1 hidden=%2 edges=%3").arg(int(state.textures)).arg(int(state.facesHidden)).arg(int(state.edges));
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	// The camera as it has always looked: textured faces, no edges.
	const LevelCameraRenderState defaults;
	expect(defaults.textures && !defaults.facesHidden && !defaults.edges, "defaults", describe(defaults));
	expect(levelCameraFaceMode(defaults) == LevelCameraFaceMode::Textured && levelCameraDrawMode(defaults) == LevelCameraDrawMode::Textured
			&& !levelCameraDrawsEdgeOverlay(defaults),
		"defaults draw textured faces without edges");

	// Face modes, as TrenchBroom's Show Textures, Hide Textures and Hide Faces.
	LevelCameraRenderState edged {true, false, true};
	const LevelCameraRenderState flat = levelCameraWithFaceMode(edged, LevelCameraFaceMode::Flat);
	expect(!flat.textures && !flat.facesHidden && flat.edges && levelCameraFaceMode(flat) == LevelCameraFaceMode::Flat, "Hide Textures keeps the edges",
		describe(flat));
	const LevelCameraRenderState hidden = levelCameraWithFaceMode(flat, LevelCameraFaceMode::Hidden);
	expect(hidden.facesHidden && !hidden.textures && hidden.edges && levelCameraFaceMode(hidden) == LevelCameraFaceMode::Hidden,
		"Hide Faces keeps the textures and edges choices", describe(hidden));
	expect(!levelCameraDrawsEdgeOverlay(hidden) && levelCameraDrawsEdgeOverlay(flat), "the edge overlay needs faces to draw over");
	const LevelCameraRenderState textured = levelCameraWithFaceMode(hidden, LevelCameraFaceMode::Textured);
	expect(textured.textures && !textured.facesHidden && textured.edges, "Show Textures brings the faces back textured", describe(textured));

	// 3D Wireframe only hides the faces; turning it off restores the look.
	LevelCameraRenderState wire = flat;
	wire.facesHidden = true;
	expect(levelCameraDrawMode(wire) == LevelCameraDrawMode::Wireframe, "hidden faces are the Wireframe mode");
	wire.facesHidden = false;
	expect(wire == flat, "turning wireframe off restores flat shading with edges", describe(wire));

	// Where each state sits in NRC's cycle.
	expect(levelCameraDrawMode({false, false, false}) == LevelCameraDrawMode::Flat && levelCameraDrawMode({false, false, true}) == LevelCameraDrawMode::Flat
			&& levelCameraDrawMode({true, false, false}) == LevelCameraDrawMode::Textured
			&& levelCameraDrawMode({true, false, true}) == LevelCameraDrawMode::TexturedEdges
			&& levelCameraDrawMode({true, true, true}) == LevelCameraDrawMode::Wireframe,
		"draw modes of every state");

	// The cycle forward and back, wrapping at both ends and for large steps.
	const LevelCameraDrawMode order[] = {LevelCameraDrawMode::Wireframe, LevelCameraDrawMode::Flat, LevelCameraDrawMode::Textured,
		LevelCameraDrawMode::TexturedEdges};
	for (int index = 0; index < kLevelCameraDrawModeCount; ++index) {
		const LevelCameraDrawMode mode = order[index];
		expect(levelCameraSteppedDrawMode(mode, 1) == order[(index + 1) % 4], "next", levelCameraDrawModeId(mode));
		expect(levelCameraSteppedDrawMode(mode, -1) == order[(index + 3) % 4], "previous", levelCameraDrawModeId(mode));
		expect(levelCameraSteppedDrawMode(mode, 4) == mode && levelCameraSteppedDrawMode(mode, -9) == order[(index + 3) % 4]
				&& levelCameraSteppedDrawMode(mode, 0) == mode,
			"whole turns and long steps", levelCameraDrawModeId(mode));
		expect(levelCameraDrawModeNumber(mode) == index + 1, "numbers count from one");
		// Stepping into a mode draws exactly that mode.
		const LevelCameraRenderState reached = levelCameraWithDrawMode({true, false, true}, mode);
		expect(levelCameraDrawMode(reached) == mode, "the state for a mode draws it", describe(reached));
		LevelCameraDrawMode parsed = LevelCameraDrawMode::Flat;
		expect(levelCameraDrawModeFromId(levelCameraDrawModeId(mode), &parsed) && parsed == mode, "identifier round trip", levelCameraDrawModeId(mode));
		expect(!levelCameraDrawModeName(mode).isEmpty() && levelCameraDrawModeAnnouncement(mode).contains(levelCameraDrawModeName(mode))
				&& levelCameraDrawModeAnnouncement(mode).contains(QString::number(index + 1)),
			"names and announcements", levelCameraDrawModeAnnouncement(mode));
	}
	expect(levelCameraDrawModeAnnouncement(LevelCameraDrawMode::Textured) == QStringLiteral("Camera mode 3 of 4: Textured."), "announcement wording",
		levelCameraDrawModeAnnouncement(LevelCameraDrawMode::Textured));
	expect(levelCameraWithDrawMode({false, false, false}, LevelCameraDrawMode::TexturedEdges) == LevelCameraRenderState {true, false, true}
			&& levelCameraWithDrawMode({true, false, true}, LevelCameraDrawMode::Flat) == LevelCameraRenderState {false, false, false}
			&& levelCameraWithDrawMode({false, false, true}, LevelCameraDrawMode::Wireframe) == LevelCameraRenderState {false, true, true},
		"modes set the faces, images and edges; Wireframe keeps the rest");
	// A full turn from the defaults visits every mode once and comes back.
	LevelCameraRenderState walk = defaults;
	QStringList visited;
	for (int step = 0; step < kLevelCameraDrawModeCount; ++step) {
		walk = levelCameraWithDrawMode(walk, levelCameraSteppedDrawMode(levelCameraDrawMode(walk), 1));
		visited << levelCameraDrawModeId(levelCameraDrawMode(walk));
	}
	expect(visited == QStringList {QStringLiteral("textured-edges"), QStringLiteral("wireframe"), QStringLiteral("flat"), QStringLiteral("textured")}
			&& walk == defaults,
		"a full turn of Next Camera Mode", visited.join(QLatin1Char(',')));
	LevelCameraDrawMode unknown = LevelCameraDrawMode::Flat;
	expect(!levelCameraDrawModeFromId(QStringLiteral("lighting"), &unknown) && unknown == LevelCameraDrawMode::Flat
			&& levelCameraDrawModeFromId(QStringLiteral(" Textured-Edges "), &unknown) && unknown == LevelCameraDrawMode::TexturedEdges,
		"identifiers are trimmed and case-blind; Doom 3 lighting is not a mode");

	// Every state has its own summary sentence.
	const LevelCameraRenderState states[] = {{true, false, false}, {true, false, true}, {false, false, false}, {false, false, true}, {true, true, false}};
	QStringList summaries;
	for (const LevelCameraRenderState& state : states) {
		summaries << levelCameraRenderSummary(state);
	}
	summaries.removeDuplicates();
	expect(summaries.size() == 5 && levelCameraRenderSummary({false, true, true}) == levelCameraRenderSummary({true, true, false}),
		"summaries name the faces and edges; hidden faces read the same", summaries.join(QLatin1Char('|')));

	if (ok) {
		std::cout << "Level camera render: face modes, wireframe restore, cycle and names passed." << std::endl;
	}
	return ok ? 0 : 1;
}
