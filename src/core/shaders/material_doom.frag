// Doom walls, flats and the sky as linuxdoom-1.10 draws them: point-sampled
// texels with vanilla wrapping (or Boom's), COLORMAP shading by sector
// light, distance and fake contrast, and ZDoom's warps.
//
// mode.x: 0 a wall or flat, 1 the sky.
// mode.z flags: 1 flat, 2 Boom wrapping, 4 COLORMAP shading, 8 palette
//   indices, 16 a 256-colour palette, 32 distance lighting, 64 fake contrast.
// mode.w: warp (0 none, 1 warp, 2 warp2).
// params[0]: x light band, y warp speed, z time.
// params[1]: x, y the repeat in texels; z largest power of two of the width.
// Slots: 0 colours, 1 indices (red), 2 palette (256 x 1), 3 COLORMAP rows.

#include "material_common.glsl"

VIBE_VARYING_IN(0) vec3 vPosition;
VIBE_VARYING_IN(1) vec3 vNormal;
VIBE_VARYING_IN(2) vec3 vTangent;
VIBE_VARYING_IN(3) vec3 vBitangent;
VIBE_VARYING_IN(4) vec4 vTexCoord;
VIBE_VARYING_IN(5) vec4 vColor;
VIBE_VARYING_IN(6) float vDepth;

VIBE_FRAGMENT_OUT(0) vec4 outColor;

int paletteIndex(ivec2 texel)
{
	return int(texelFetch(tex1, texel, 0).r * 255.0 + 0.5);
}

vec3 paletteColor(int index)
{
	return texelFetch(tex2, ivec2(index & 255, 0), 0).rgb;
}

// r_main.c zlight / scalelight: the colormap row for a view depth.
int lightLevel(float depth, int contrast)
{
	bool isFlat = (mode.z & 1) != 0;
	int band = int(params[0].x + 0.5);
	int startmap = 4 * (15 - clamp(band + contrast, 0, 15));
	float d = (mode.z & 32) != 0 ? max(1.0, depth) : 128.0;
	if (isFlat) {
		int j = clamp(int(d / 16.0), 0, 127);
		return clamp(startmap - (160 / (j + 1)) / 2, 0, 31);
	}
	int j = clamp(int(2560.0 / d), 0, 47);
	return clamp(startmap - j / 2, 0, 31);
}

void main()
{
	ivec2 size = textureSize(tex0, 0);
	bool hasIndices = (mode.z & 8) != 0;
	if (mode.x == 1) {
		// r_plane.c: 1024 sky columns a turn, mirrored; rows from screen
		// height, unlit.
		vec3 d = pixelRay(gl_FragCoord.xy);
		float yaw = atan(d.y, d.x);
		int column = int(floor(yaw / (2.0 * kPi) * 1024.0));
		column = column & (int(params[1].z + 0.5) - 1);
		float horizontal = max(1.0e-6, sqrt(d.x * d.x + d.y * d.y));
		int row = int(floor(100.0 - 160.0 * d.z / horizontal)) & 127;
		row = min(row, size.y - 1);
		ivec2 texel = ivec2(clamp(column, 0, size.x - 1), row);
		vec3 sky = hasIndices ? paletteColor(paletteIndex(texel)) : texelFetch(tex0, texel, 0).rgb;
		outColor = vec4(sky, 1.0);
		return;
	}
	float s = vTexCoord.x;
	float t = vTexCoord.y;
	int warp = mode.w;
	if (warp != 0) {
		float w = params[1].x;
		float h = params[1].y;
		float u = s / w;
		float v = t / h;
		float now = params[0].z * params[0].y;
		if (warp == 1) {
			// func_warp1.fp: one wave per repeat, an eight-second cycle.
			float du = 0.1 * sin(2.0 * kPi * (v + now / 8.0));
			float dv = 0.1 * sin(2.0 * kPi * (u + now / 8.0));
			u += du;
			v += dv;
		} else {
			// func_warp2.fp: two summed waves with Eternity's phase constants.
			float u0 = u;
			float v0 = v;
			u += 0.025 * (0.5 + sin(2.0 * kPi * (v0 + 0.49 * now + 700.0 / 8192.0)) + sin(2.0 * kPi * (2.0 * u0 + 0.49 * now + 1200.0 / 8192.0)));
			v += 0.025 * (0.5 + sin(2.0 * kPi * (v0 + 0.61 * now + 900.0 / 8192.0)) + sin(2.0 * kPi * (2.0 * u0 + 0.36 * now + 300.0 / 8192.0)));
		}
		s = u * w;
		t = v * h;
	}
	// Vanilla wrapping: columns at the largest power of two not above the
	// width, rows at 128 whatever the height; Boom wraps properly; flats
	// tile every 64.
	int column = int(floor(s));
	int row = int(floor(t));
	int x = 0;
	int y = 0;
	bool garbage = false;
	if ((mode.z & 1) != 0) {
		x = wrapIndex(wrapIndex(column, 64), max(1, size.x));
		y = wrapIndex(wrapIndex(row, 64), max(1, size.y));
	} else if ((mode.z & 2) != 0) {
		x = wrapIndex(column, size.x);
		y = wrapIndex(row, size.y);
	} else {
		x = column & (int(params[1].z + 0.5) - 1);
		y = row & 127;
		garbage = y >= size.y;
	}
	int index = -1;
	vec3 color = vec3(0.0);
	if (garbage) {
		// Vanilla reads past the column into other memory.
		index = int((uint(x) * 2654435761u ^ uint(y) * 40503u) % 256u);
	} else {
		vec4 texel = texelFetch(tex0, ivec2(x, y), 0);
		if (texel.a <= 0.0) {
			// Holes in a masked texture show what is behind.
			discard;
		}
		index = hasIndices ? paletteIndex(ivec2(x, y)) : -1;
		color = texel.rgb;
	}
	// Fake contrast: walls along X are darker, along Y brighter.
	int contrast = 0;
	if ((mode.z & 1) == 0 && (mode.z & 64) != 0) {
		if (abs(vNormal.x) > 0.99) {
			contrast = 1;
		} else if (abs(vNormal.y) > 0.99) {
			contrast = -1;
		}
	}
	int level = lightLevel(vDepth, contrast);
	if ((mode.z & 4) != 0 && index >= 0) {
		int shaded = int(texelFetch(tex3, ivec2(index, level), 0).r * 255.0 + 0.5);
		color = paletteColor(shaded);
	} else {
		float factor = 1.0 - float(level) / 32.0;
		vec3 base = index >= 0 && (mode.z & 16) != 0 ? paletteColor(index) : color;
		color = floor(base * 255.0 * factor) / 255.0;
	}
	outColor = vec4(color, 1.0);
}
