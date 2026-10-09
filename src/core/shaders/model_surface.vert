// Model and level surfaces: one draw per texture group of world-space
// triangles, three unshared corners each, in the viewport's triangle order.
// Facing, flat shading and back-face culling follow the face plane carried
// on every corner, so all three corners of a triangle agree.

#include "common.glsl"

VIBE_ATTRIBUTE(0) vec3 aPosition;
VIBE_ATTRIBUTE(1) vec2 aUv;
// Unit face normal (turned to agree with the format's vertex normals) and
// its dot product with the first corner.
VIBE_ATTRIBUTE(2) vec4 aPlane;
// The surface's flat colour.
VIBE_ATTRIBUTE(3) vec4 aColor;
// Bit 0: the corner has finite texture coordinates on a textured surface.
// Bit 1: the triangle is degenerate or not finite and draws nothing.
VIBE_ATTRIBUTE(4) uint aInfo;
// Bit 0: highlighted (selected or stroked). Bits 2..4: the edge opposite
// corner 0, 1 or 2 is selected.
VIBE_ATTRIBUTE(5) uint aFlags;

VIBE_UNIFORMS {
	mat4 viewProjection;
	// xyz: toward the viewer (orthographic) or the eye (perspective).
	// w: 1 for perspective.
	vec4 cameraEye;
	vec4 light;
	vec4 hatchColor;
	vec4 edgeColor;
	vec4 hoverColor;
	vec4 selectionColor;
	vec4 highlightColor;
	// x: pixel ratio; y: 1 draws every edge; z: 1 samples tex0.
	vec4 params;
	// x: hovered triangle; y: first triangle of this draw; z: 1 culls back
	// faces; w: 0 opaque pass, 1 first translucent layer, 2 a deeper layer.
	ivec4 iparams;
};

VIBE_VARYING_OUT(0) vec2 vUv;
VIBE_VARYING_OUT(1) vec3 vBarycentricW;
// Untextured: the shaded flat colour. Textured: r holds the light factor.
VIBE_FLAT_OUT(2) vec4 vShade;
VIBE_FLAT_OUT(3) int vTriangle;
// Bit 0 highlighted, 1 hovered, 2..4 selected edges, 5 textured.
VIBE_FLAT_OUT(4) uint vFlags;

void main()
{
	int corner = VIBE_VERTEX_INDEX % 3;
	int triangle = iparams.y + VIBE_VERTEX_INDEX / 3;
	vec3 normal = aPlane.xyz;
	bool perspective = cameraEye.w > 0.5;
	float facing = perspective ? dot(normal, cameraEye.xyz) - aPlane.w : dot(normal, cameraEye.xyz);
	bool front = facing > 0.0;
	vUv = aUv;
	vTriangle = triangle;
	if ((aInfo & 2u) != 0u || (iparams.z != 0 && !front)) {
		gl_Position = VIBE_DISCARDED_POSITION;
		vBarycentricW = vec3(0.0);
		vShade = vec4(0.0);
		vFlags = 0u;
		return;
	}
	// Two-sided sheets are lit from whichever side faces the camera, so the
	// back of a flag is not a black hole.
	float lambert = dot(normal, light.xyz);
	if (!front) {
		lambert = -lambert;
	}
	float shade = clamp(lambert, 0.0, 1.0);
	// 24 shade steps, as the studio has always drawn flat faces.
	float t = floor(shade * 23.0 + 0.5) / 23.0;
	bool highlighted = (aFlags & 1u) != 0u;
	bool textured = params.z > 0.5 && (aInfo & 1u) != 0u;
	if (textured) {
		vShade = vec4(1.0 - (1.0 - t) * 135.0 / 255.0, 0.0, 0.0, 1.0);
	} else if (highlighted) {
		vShade = vec4(floor(highlightColor.rgb * 255.0 * (0.45 + 0.55 * t) + 0.5) / 255.0, 1.0);
	} else {
		vShade = vec4(floor(aColor.rgb * 255.0 * (0.22 + 0.78 * t) + 0.5) / 255.0, 1.0);
	}
	uint flags = (aFlags & 29u) | (textured ? 32u : 0u) | (triangle == iparams.x ? 2u : 0u);
	vFlags = flags;
	vec4 clip = viewProjection * vec4(aPosition, 1.0);
	// Screen-linear barycentrics: the fragment multiplies by gl_FragCoord.w,
	// undoing the perspective division of the interpolation.
	vec3 barycentric = vec3(corner == 0 ? 1.0 : 0.0, corner == 1 ? 1.0 : 0.0, corner == 2 ? 1.0 : 0.0);
	vBarycentricW = barycentric * clip.w;
	gl_Position = VIBE_CLIP(clip);
}
