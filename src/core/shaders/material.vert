// Material preview shapes: world-space vertices with the frame each engine
// shades with, and per-stage coordinates and colours worked out on the CPU
// the way the engines compute them per vertex. Every pass of a frame draws
// the same positions through this program; `invariant` keeps their depth
// identical so equal-depth passes line up exactly.

#include "material_common.glsl"

VIBE_ATTRIBUTE(0) vec3 aPosition;
VIBE_ATTRIBUTE(1) vec3 aNormal;
VIBE_ATTRIBUTE(2) vec3 aTangent;
VIBE_ATTRIBUTE(3) vec3 aBitangent;
// xy: the stage's texture coordinates; zw: lightmap coordinates.
VIBE_ATTRIBUTE(4) vec4 aTexCoord;
VIBE_ATTRIBUTE(5) vec4 aColor;

VIBE_VARYING_OUT(0) vec3 vPosition;
VIBE_VARYING_OUT(1) vec3 vNormal;
VIBE_VARYING_OUT(2) vec3 vTangent;
VIBE_VARYING_OUT(3) vec3 vBitangent;
VIBE_VARYING_OUT(4) vec4 vTexCoord;
VIBE_VARYING_OUT(5) vec4 vColor;
// Distance along the view direction, as the engines' depth.
VIBE_VARYING_OUT(6) float vDepth;

invariant gl_Position;

void main()
{
	vPosition = aPosition;
	vNormal = aNormal;
	vTangent = aTangent;
	vBitangent = aBitangent;
	vTexCoord = aTexCoord;
	vColor = aColor;
	vDepth = dot(aPosition - eye.xyz, forward.xyz);
	gl_Position = VIBE_CLIP(viewProjection * vec4(aPosition, 1.0));
}
