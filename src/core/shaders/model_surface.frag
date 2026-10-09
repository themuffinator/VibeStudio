// Model and level surfaces. The opaque pass keeps fully opaque fragments
// and writes the colour, the triangle under each pixel and its depth. The
// translucent passes peel the remaining fragments front to back, one layer
// per pass, so crossing translucent faces composite in depth order.
//
// Colours are premultiplied. Hatching, selected edges, edges and the hovered
// outline follow the studio's long-standing rules: widths in logical pixels,
// patterns along screen diagonals.

#include "common.glsl"

VIBE_UNIFORMS {
	mat4 viewProjection;
	vec4 cameraEye;
	vec4 light;
	vec4 hatchColor;
	vec4 edgeColor;
	vec4 hoverColor;
	vec4 selectionColor;
	vec4 highlightColor;
	vec4 params;
	ivec4 iparams;
};

// The surface's skin, premultiplied.
VIBE_SAMPLER(0) tex0;
// Translucent passes: the opaque pass's depth, and the previous layer's.
VIBE_SAMPLER(1) tex1;
VIBE_SAMPLER(2) tex2;

VIBE_VARYING_IN(0) vec2 vUv;
VIBE_VARYING_IN(1) vec3 vBarycentricW;
VIBE_FLAT_IN(2) vec4 vShade;
VIBE_FLAT_IN(3) int vTriangle;
VIBE_FLAT_IN(4) uint vFlags;

VIBE_FRAGMENT_OUT(0) vec4 outColor;
VIBE_FRAGMENT_OUT(1) int outTriangle;
VIBE_FRAGMENT_OUT(2) float outDepth;

// Mixes an overlay colour (straight, its alpha the amount) into a
// premultiplied colour without changing its coverage.
vec4 tint(vec4 base, vec4 overlay)
{
	return vec4(base.rgb * (1.0 - overlay.a) + overlay.rgb * base.a * overlay.a, base.a);
}

void main()
{
	// Distances to the three edges, in pixels, from screen-linear
	// barycentrics. Derivatives come first, before any fragment is dropped.
	vec3 barycentric = vBarycentricW * gl_FragCoord.w;
	vec3 gradient = vec3(length(vec2(dFdx(barycentric.x), dFdy(barycentric.x))),
		length(vec2(dFdx(barycentric.y), dFdy(barycentric.y))),
		length(vec2(dFdx(barycentric.z), dFdy(barycentric.z))));
	vec3 edgeDistance = barycentric / max(gradient, vec3(1.0e-20));

	vec4 color;
	if ((vFlags & 32u) != 0u) {
		vec4 texel = texture(tex0, vUv);
		if (floor(texel.a * 255.0 + 0.5) <= 0.0) {
			discard;
		}
		color = vec4(floor(texel.rgb * 255.0 * vShade.r + 0.5) / 255.0, texel.a);
	} else {
		color = vShade;
	}

	float alpha = floor(color.a * 255.0 + 0.5);
	int pass = iparams.w;
	if (pass == 0) {
		if (alpha < 255.0) {
			discard;
		}
	} else {
		if (alpha >= 255.0) {
			discard;
		}
		ivec2 pixel = ivec2(gl_FragCoord.xy);
		if (gl_FragCoord.z <= texelFetch(tex1, pixel, 0).r) {
			discard;
		}
		if (pass == 2 && gl_FragCoord.z >= texelFetch(tex2, pixel, 0).r) {
			discard;
		}
	}

	float ratio = params.x;
	float diagonal = (gl_FragCoord.x + gl_FragCoord.y) / ratio;
	if ((vFlags & 1u) != 0u && mod(diagonal, 8.0) < 1.2) {
		color = tint(color, hatchColor);
	}
	bool marked = false;
	for (int edge = 0; edge < 3; ++edge) {
		if ((vFlags & (4u << uint(edge))) != 0u && edgeDistance[edge] <= 2.8 * ratio && mod(diagonal, 9.0) < 6.0) {
			color = tint(color, selectionColor);
			marked = true;
			break;
		}
	}
	bool hovered = (vFlags & 2u) != 0u;
	if (!marked && (params.y > 0.5 || hovered)) {
		float width = (hovered ? 1.8 : 0.9) * ratio;
		for (int edge = 0; edge < 3; ++edge) {
			if (edgeDistance[edge] <= width) {
				color = tint(color, hovered ? hoverColor : edgeColor);
				break;
			}
		}
	}
	outColor = color;
	outTriangle = vTriangle;
	outDepth = gl_FragCoord.z;
}
