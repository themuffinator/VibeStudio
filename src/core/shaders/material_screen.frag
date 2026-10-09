// Whole-image passes of a material preview, drawn with fullscreen.vert.
//
// mode.x:
//   0  the background: params[0].rgb the colour (already scaled), with a
//      12-pixel checker 18% brighter when params[0].w is 1; alpha 0
//   1  Quake III's fog curve over the fog volume's walls: tex0 holds each
//      pixel's distance (1e30 where nothing was drawn), params[0].x the
//      distance to opaque, color the fog colour. Blended as source alpha.

#include "material_common.glsl"

VIBE_VARYING_IN(0) vec2 vUv;

VIBE_FRAGMENT_OUT(0) vec4 outColor;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	if (mode.x == 1) {
		float distanceToWall = texelFetch(tex0, pixel, 0).r;
		float amount = distanceToWall < 1.0e29 ? sqrt(min(1.0, distanceToWall / max(1.0, params[0].x))) : 1.0;
		outColor = vec4(color.rgb, amount);
		return;
	}
	float shade = params[0].w > 0.5 && (pixel.x / 12 + pixel.y / 12) % 2 == 0 ? 1.18 : 1.0;
	outColor = vec4(min(vec3(1.0), params[0].rgb * shade), 0.0);
}
