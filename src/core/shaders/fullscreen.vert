// One triangle covering the target, with no vertex input. vUv runs from
// (0, 0) at the top-left corner to (1, 1) at the bottom-right.

#include "common.glsl"

VIBE_VARYING_OUT(0) vec2 vUv;

void main()
{
	vec2 corner = vec2(float((VIBE_VERTEX_INDEX << 1) & 2), float(VIBE_VERTEX_INDEX & 2));
	vUv = corner;
	gl_Position = VIBE_CLIP(vec4(corner * 2.0 - 1.0, 0.0, 1.0));
}
