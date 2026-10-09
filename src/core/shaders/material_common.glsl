// Shared by the material preview programs (core/material_render*.cpp).
//
// One uniform block serves every material program, so the C++ side packs a
// single layout (MaterialUniforms in core/material_render_p.h). Sampling is
// done here rather than by sampler state, so wrap rules the engines use
// (zero and alpha-zero clamps), explicit mip choice and bilinear weights
// match the engines' own arithmetic.

#include "common.glsl"

VIBE_UNIFORMS {
	mat4 viewProjection;
	// xyz: the camera's eye; w: 1 for an orthographic view.
	vec4 eye;
	// xyz: view direction; w: focal length in pixels.
	vec4 forward;
	// xyz: screen right; w: orthographic pixels per unit.
	vec4 right;
	// xyz: screen up; w: unused.
	vec4 up;
	// xy: target size in pixels; z: display scale; w: identity light.
	vec4 viewport;
	// xyz: the preview light; w: lightmap level.
	vec4 light;
	// xyz: the shape's centre; w: its radius.
	vec4 mesh;
	// x: 1 for a falloff spot; y: overbright shift; z: time; w: texels per
	// world unit of the stage image.
	vec4 lighting;
	vec4 lightColor;
	// x: program mode; y: alpha test; z: flags; w: a mode-specific selector.
	ivec4 mode;
	vec4 color;
	vec4 params[8];
	// Two rows (2x3 texture matrices, xyz used) a slot.
	vec4 matrices[16];
	// Per sampler: x wrap (0 repeat, 1 clamp, 2 zero, 3 alpha zero);
	// y mip levels; z, w mode-specific.
	ivec4 samplerInfo[8];
};

VIBE_SAMPLER(0) tex0;
VIBE_SAMPLER(1) tex1;
VIBE_SAMPLER(2) tex2;
VIBE_SAMPLER(3) tex3;
VIBE_SAMPLER(4) tex4;
VIBE_SAMPLER(5) tex5;
VIBE_SAMPLER(6) tex6;
VIBE_SAMPLER(7) tex7;

const float kPi = 3.14159265358979323846;

vec4 fetchSlot(int slot, ivec2 texel, int level)
{
	if (slot == 0) {
		return texelFetch(tex0, texel, level);
	}
	if (slot == 1) {
		return texelFetch(tex1, texel, level);
	}
	if (slot == 2) {
		return texelFetch(tex2, texel, level);
	}
	if (slot == 3) {
		return texelFetch(tex3, texel, level);
	}
	if (slot == 4) {
		return texelFetch(tex4, texel, level);
	}
	if (slot == 5) {
		return texelFetch(tex5, texel, level);
	}
	if (slot == 6) {
		return texelFetch(tex6, texel, level);
	}
	return texelFetch(tex7, texel, level);
}

ivec2 slotSize(int slot, int level)
{
	if (slot == 0) {
		return textureSize(tex0, level);
	}
	if (slot == 1) {
		return textureSize(tex1, level);
	}
	if (slot == 2) {
		return textureSize(tex2, level);
	}
	if (slot == 3) {
		return textureSize(tex3, level);
	}
	if (slot == 4) {
		return textureSize(tex4, level);
	}
	if (slot == 5) {
		return textureSize(tex5, level);
	}
	if (slot == 6) {
		return textureSize(tex6, level);
	}
	return textureSize(tex7, level);
}

// Modulo that stays positive for negative values.
int wrapIndex(int value, int size)
{
	return value - size * int(floor(float(value) / float(size)));
}

vec4 texelWrapped(int slot, int x, int y, int level, ivec2 size, int wrap)
{
	if (wrap == 0) {
		x = wrapIndex(x, size.x);
		y = wrapIndex(y, size.y);
	} else if (wrap == 1) {
		x = clamp(x, 0, size.x - 1);
		y = clamp(y, 0, size.y - 1);
	} else if (wrap == 2) {
		if (x < 0 || y < 0 || x >= size.x || y >= size.y) {
			return vec4(0.0);
		}
	} else {
		bool outside = x < 0 || y < 0 || x >= size.x || y >= size.y;
		vec4 edge = fetchSlot(slot, ivec2(clamp(x, 0, size.x - 1), clamp(y, 0, size.y - 1)), level);
		if (outside) {
			edge.a = 0.0;
		}
		return edge;
	}
	return fetchSlot(slot, ivec2(x, y), level);
}

// The mip level an engine would pick for this many texels a pixel.
int slotLevel(int slot, float lod)
{
	int levels = max(1, samplerInfo[slot].y);
	if (lod >= 0.5 && levels > 1) {
		return min(levels - 1, int(floor(lod + 0.5)));
	}
	return 0;
}

// Straight RGBA at (s, t) in texture repeats, wrapped as the slot says.
vec4 sampleSlotWrapped(int slot, vec2 st, bool bilinear, float lod, int wrap)
{
	int level = slotLevel(slot, lod);
	ivec2 size = slotSize(slot, level);
	if (isnan(st.x) || isnan(st.y) || isinf(st.x) || isinf(st.y)) {
		st = vec2(0.0);
	}
	if (abs(st.x) > 1.0e6 || abs(st.y) > 1.0e6) {
		st = fract(st);
	}
	if (!bilinear) {
		return texelWrapped(slot, int(floor(st.x * float(size.x))), int(floor(st.y * float(size.y))), level, size, wrap);
	}
	vec2 uv = st * vec2(size) - 0.5;
	vec2 base = floor(uv);
	ivec2 p = ivec2(base);
	vec2 f = uv - base;
	vec4 c00 = texelWrapped(slot, p.x, p.y, level, size, wrap);
	vec4 c10 = texelWrapped(slot, p.x + 1, p.y, level, size, wrap);
	vec4 c01 = texelWrapped(slot, p.x, p.y + 1, level, size, wrap);
	vec4 c11 = texelWrapped(slot, p.x + 1, p.y + 1, level, size, wrap);
	return c00 * ((1.0 - f.x) * (1.0 - f.y)) + c10 * (f.x * (1.0 - f.y)) + c01 * ((1.0 - f.x) * f.y) + c11 * (f.x * f.y);
}

vec4 sampleSlot(int slot, vec2 st, bool bilinear, float lod)
{
	return sampleSlotWrapped(slot, st, bilinear, lod, samplerInfo[slot].x);
}

// A slot's 2x3 texture matrix applied to (s, t).
vec2 applyMatrix(int slot, vec2 st)
{
	vec4 row0 = matrices[slot * 2];
	vec4 row1 = matrices[slot * 2 + 1];
	return vec2(row0.x * st.x + row0.y * st.y + row0.z, row1.x * st.x + row1.y * st.y + row1.z);
}

// Texels a pixel covers for a texture of this density, as a mip level.
float mipLevel(float depth, float obliquity, float texelsPerUnit)
{
	float unitsPerPixel = eye.w > 0.5 ? 1.0 / max(1.0e-6, right.w) : depth / max(1.0e-6, forward.w);
	float texelsPerPixel = unitsPerPixel * texelsPerUnit / clamp(obliquity, 0.2, 1.0);
	return texelsPerPixel <= 1.0 ? 0.0 : log2(texelsPerPixel);
}

// The world direction through a pixel centre.
vec3 pixelRay(vec2 pixel)
{
	if (eye.w > 0.5) {
		return forward.xyz;
	}
	vec2 centre = viewport.xy * 0.5;
	return normalize(forward.xyz + right.xyz * ((pixel.x - centre.x) / forward.w) + up.xyz * (-(pixel.y - centre.y) / forward.w));
}

// The preview's soft light on a point: 1.0 is full brightness.
float previewLight(vec3 position, vec3 normal)
{
	float level = max(0.0, light.w);
	if (lighting.x < 0.5) {
		return level;
	}
	vec3 toLight = light.xyz - position;
	float distanceToLight = length(toLight);
	float facing = max(0.0, dot(normal, toLight / max(1.0e-6, distanceToLight)));
	float falloff = clamp(1.0 - distanceToLight / (mesh.w * 2.4), 0.0, 1.0);
	return level * (0.42 + 0.95 * facing * falloff);
}

// Quake III's lightmap texel for a level, after its overbright shift, scaled
// down together when a channel overflows so the hue holds.
vec4 quake3Lightmap(float level)
{
	float stored = level / 4.0 * exp2(lighting.y);
	vec3 value = stored * lightColor.rgb;
	float maximum = max(value.r, max(value.g, value.b));
	if (maximum > 1.0) {
		value /= maximum;
	}
	return vec4(value, 1.0);
}

// Quake III's sine table: 1024 entries over sin(i * 360 / 1023 degrees).
float quake3Sin(int index)
{
	return sin(float(index & 1023) * (2.0 * kPi / 1023.0));
}

bool alphaPasses(int test, float alpha)
{
	if (test == 1) {
		return alpha > 0.0;
	}
	if (test == 2) {
		return alpha < 0.5;
	}
	if (test == 3) {
		return alpha >= 0.5;
	}
	return true;
}

// tr_sky.c vec_to_st: per face, s and t as signed components divided by the
// major axis; faces rt bk lf ft up dn are +X +Y -X -Y +Z -Z.
float component(vec3 v, int selector)
{
	int axis = abs(selector) - 1;
	float value = axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
	return selector < 0 ? -value : value;
}

// Samples a sky box held in samplerInfo 0..5 for a world direction (Z up).
vec4 skyBox(vec3 direction)
{
	ivec3 table[6] = ivec3[6](ivec3(-2, 3, 1), ivec3(2, 3, -1), ivec3(1, 3, 2), ivec3(-1, 3, -2), ivec3(-2, -1, 3), ivec3(-2, 1, -3));
	vec3 a = abs(direction);
	int face = 0;
	if (a.x >= a.y && a.x >= a.z) {
		face = direction.x < 0.0 ? 1 : 0;
	} else if (a.y >= a.z) {
		face = direction.y < 0.0 ? 3 : 2;
	} else {
		face = direction.z < 0.0 ? 5 : 4;
	}
	if (samplerInfo[face].y <= 0) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	ivec3 entry = table[face];
	float dv = component(direction, entry.z);
	if (dv <= 1.0e-9) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	float s = component(direction, entry.x) / dv;
	float t = component(direction, entry.y) / dv;
	vec2 st = vec2(clamp((s + 1.0) / 2.0, 0.0, 1.0), clamp(1.0 - (t + 1.0) / 2.0, 0.0, 1.0));
	return sampleSlotWrapped(face, st, true, 0.0, 1);
}

// Samples a Doom 3 cube map held in samplerInfo 0..5 (+X, -X, +Y, -Y, +Z, -Z) with
// the OpenGL face rules, the direction used as given.
vec4 cubeMap(vec3 direction)
{
	vec3 a = abs(direction);
	int face = 0;
	float sc = 0.0;
	float tc = 0.0;
	float ma = 1.0;
	if (a.x >= a.y && a.x >= a.z) {
		face = direction.x >= 0.0 ? 0 : 1;
		ma = a.x;
		sc = direction.x >= 0.0 ? -direction.z : direction.z;
		tc = -direction.y;
	} else if (a.y >= a.z) {
		face = direction.y >= 0.0 ? 2 : 3;
		ma = a.y;
		sc = direction.x;
		tc = direction.y >= 0.0 ? direction.z : -direction.z;
	} else {
		face = direction.z >= 0.0 ? 4 : 5;
		ma = a.z;
		sc = direction.z >= 0.0 ? direction.x : -direction.x;
		tc = -direction.y;
	}
	if (samplerInfo[face].y <= 0 || ma <= 0.0) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	return sampleSlotWrapped(face, vec2((sc / ma + 1.0) / 2.0, (tc / ma + 1.0) / 2.0), true, 0.0, 1);
}
