// Quake and Quake II surfaces: lightmaps and light styles, the software
// renderer's colormap and fullbrights, modern ports' overbright and glow
// companions, liquid warps, two-layer skies and Quake II's intensity,
// translucency, flowing scroll and sky boxes.
//
// mode.z flags: 1 bilinear, 2 Quake II, 4 sky, 8 warp, 16 flowing,
//   32 software renderer, 64 COLORMAP shading, 128 overbright (modern port),
//   256 fullbrights, 512 palette indices, 1024 a glow companion, 2048
//   masked, 4096 a Quake II sky box in samplerInfo 0..5, 8192 a usable image.
// params[0]: x translucency, y Quake II intensity, z time, w light style.
// params[1]: x lightmap sample (0..255); y, z the texture size in texels.
// Slots: 0 colours (mipmapped), 1 indices, 2 palette, 3 colormap.lmp rows,
//   4 the glow companion; or 0..5 the sky box.

#include "material_common.glsl"

VIBE_VARYING_IN(0) vec3 vPosition;
VIBE_VARYING_IN(1) vec3 vNormal;
VIBE_VARYING_IN(2) vec3 vTangent;
VIBE_VARYING_IN(3) vec3 vBitangent;
VIBE_VARYING_IN(4) vec4 vTexCoord;
VIBE_VARYING_IN(5) vec4 vColor;
VIBE_VARYING_IN(6) float vDepth;

VIBE_FRAGMENT_OUT(0) vec4 outColor;

bool flag(int bit)
{
	return (mode.z & bit) != 0;
}

vec3 paletteColor(int index)
{
	return texelFetch(tex2, ivec2(index & 255, 0), 0).rgb;
}

// The palette index under (s, t), point sampled and repeated; -1 where the
// texel is transparent or the image has no indices.
int sampleIndex(vec2 st)
{
	ivec2 size = textureSize(tex1, 0);
	if (!flag(512) || isnan(st.x) || isnan(st.y)) {
		return -1;
	}
	ivec2 texel = ivec2(wrapIndex(int(floor(st.x * float(size.x))), size.x), wrapIndex(int(floor(st.y * float(size.y))), size.y));
	if (texelFetch(tex0, texel, 0).a <= 0.0) {
		return -1;
	}
	return int(texelFetch(tex1, texel, 0).r * 255.0 + 0.5);
}

// R_InitSky: the right half is the back layer, the left half the front layer
// with palette index 0 transparent.
vec4 skyTexel(int x, int y, bool front)
{
	ivec2 size = textureSize(tex0, 0);
	int halfWidth = max(1, size.x / 2);
	int x0 = front ? 0 : halfWidth;
	ivec2 texel = ivec2(x0 + wrapIndex(x, halfWidth), wrapIndex(y, size.y));
	if (flag(512)) {
		int index = int(texelFetch(tex1, texel, 0).r * 255.0 + 0.5);
		if (front && index == 0) {
			return vec4(0.0);
		}
		return vec4(paletteColor(index), 1.0);
	}
	return texelFetch(tex0, texel, 0);
}

vec4 skyLayer(vec2 st, bool front, bool bilinear)
{
	ivec2 size = textureSize(tex0, 0);
	int halfWidth = max(1, size.x / 2);
	float u = st.x * float(halfWidth);
	float v = st.y * float(size.y);
	if (!bilinear) {
		return skyTexel(int(floor(u)), int(floor(v)), front);
	}
	int x = int(floor(u - 0.5));
	int y = int(floor(v - 0.5));
	float fx = u - 0.5 - float(x);
	float fy = v - 0.5 - float(y);
	return skyTexel(x, y, front) * ((1.0 - fx) * (1.0 - fy)) + skyTexel(x + 1, y, front) * (fx * (1.0 - fy))
		+ skyTexel(x, y + 1, front) * ((1.0 - fx) * fy) + skyTexel(x + 1, y + 1, front) * (fx * fy);
}

void main()
{
	bool bilinear = flag(1);
	bool quake2 = flag(2);
	float time = params[0].z;
	vec3 position = vPosition;
	vec3 normal = normalize(vNormal);
	if (flag(4)) {
		vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
		if (quake2) {
			if (flag(4096)) {
				color = vec4(skyBox(position - eye.xyz).rgb, 1.0);
			}
		} else if (flag(8192)) {
			// EmitSkyPolys: flatten the dome by 3 in z, project to radius 378,
			// scroll the back layer 8 and the front 16 texels a second.
			vec3 d = position - eye.xyz;
			d.z *= 3.0;
			float length3 = max(1.0e-6, length(d));
			d.x *= 378.0 / length3;
			d.y *= 378.0 / length3;
			float backScroll = time * 8.0;
			backScroll -= float(int(backScroll) & ~127);
			float frontScroll = time * 16.0;
			frontScroll -= float(int(frontScroll) & ~127);
			vec4 back = skyLayer(vec2((backScroll + d.x) / 128.0, (backScroll + d.y) / 128.0), false, bilinear);
			vec4 front = skyLayer(vec2((frontScroll + d.x) / 128.0, (frontScroll + d.y) / 128.0), true, bilinear);
			color = vec4(back.rgb * (1.0 - front.a) + front.rgb * front.a, 1.0);
		}
		outColor = color;
		return;
	}
	float width = params[1].y;
	float height = params[1].z;
	float os = vTexCoord.x;
	float ot = vTexCoord.y;
	float s = os / width;
	float t = ot / height;
	float translucency = params[0].x;
	if (flag(8)) {
		if (flag(32) && !quake2) {
			// d_scan.c: a 128-texel cycle at 20 steps a second, wrapping at 64.
			int k = int(time * 20.0) & 127;
			float turbS = 8.0 + 8.0 * sin(2.0 * kPi * (floor(ot) + float(k)) / 128.0);
			float turbT = 8.0 + 8.0 * sin(2.0 * kPi * (floor(os) + float(k)) / 128.0);
			int sx = wrapIndex(int(floor(os + turbS)), 64);
			int ty = wrapIndex(int(floor(ot + turbT)), 64);
			s = (float(sx) + 0.5) / width;
			t = (float(ty) + 0.5) / height;
		} else {
			// EmitWaterPolys: (os + 8 sin(ot / 8 + time)) / 64; Quake II adds
			// the flowing scroll before dividing.
			float scroll = quake2 && flag(16) ? -64.0 * fract(time / 2.0) : 0.0;
			s = (os + 8.0 * sin(ot * 0.125 + time) + scroll) / 64.0;
			t = (ot + 8.0 * sin(os * 0.125 + time)) / 64.0;
		}
	} else if (quake2 && flag(16) && translucency >= 1.0) {
		float scroll = -64.0 * fract(time / 40.0);
		if (scroll == 0.0) {
			scroll = -64.0;
		}
		s += scroll;
	}
	vec3 view = normalize(position - eye.xyz);
	float obliquity = abs(dot(normal, view));
	float lod = mipLevel(vDepth, obliquity, 1.0);
	int index = -1;
	vec4 sampled;
	bool software = flag(32);
	if (software || flag(64)) {
		index = sampleIndex(vec2(s, t));
		if (index < 0) {
			discard;
		}
		sampled = vec4(paletteColor(index), 1.0);
	} else {
		sampled = sampleSlotWrapped(0, vec2(s, t), bilinear, min(lod, 3.0), 0);
		index = sampleIndex(vec2(s, t));
		if (sampled.a <= 0.0) {
			discard;
		}
		if (sampled.a < 0.5 && flag(2048)) {
			discard;
		}
	}
	vec4 color = sampled;
	if (quake2) {
		// gl_image.c: world textures are scaled by intensity and clamped;
		// unlit surfaces are drawn at 1 / intensity.
		float intensity = params[0].y;
		vec3 boosted = min(vec3(1.0), sampled.rgb * intensity);
		if (flag(8) || translucency < 1.0) {
			color.rgb = boosted / intensity;
		} else {
			float level = clamp(previewLight(position, normal) * 0.5, 0.0, 1.0);
			color.rgb = boosted * level;
		}
		color.a = translucency < 1.0 ? translucency : 1.0;
		outColor = vec4(clamp(color.rgb, 0.0, 1.0), color.a);
		return;
	}
	if (!flag(8)) {
		// The lightmap sample under this pixel, times the light style.
		float level = previewLight(position, normal);
		float sampleValue = clamp(params[1].x * level, 0.0, 255.0);
		float bl = sampleValue * params[0].w;
		bool fullbright = flag(256) && index >= 224;
		if (flag(64) && index >= 0) {
			// r_surf.c: rows 0 (bright) to 63 (dark) of colormap.lmp.
			int lightValue = max(64, int(65280.0 - bl) >> 2);
			int row = clamp(lightValue >> 8, 0, 63);
			int shaded = int(texelFetch(tex3, ivec2(index, row), 0).r * 255.0 + 0.5);
			color = vec4(paletteColor(shaded), 1.0);
		} else if (!fullbright) {
			// GLQuake stores min(255, bl >> 7): no overbright. Modern ports
			// store min(255, bl >> 8) and double it at draw time.
			float factor = flag(128) ? min(255.0, bl / 256.0) / 255.0 * 2.0 : min(255.0, bl / 128.0) / 255.0;
			color = sampled * factor;
		}
		if (flag(128) && flag(1024)) {
			vec4 added = sampleSlotWrapped(4, vec2(s, t), bilinear, min(lod, 3.0), 0);
			color += added * added.a;
		}
	}
	outColor = vec4(clamp(color.rgb, 0.0, 1.0), 1.0);
}
