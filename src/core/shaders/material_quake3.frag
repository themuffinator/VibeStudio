// Quake III materials: one draw a stage, blended and depth-tested by the
// stage's own state. Also the fog volume seen from inside, and sky surfaces.
//
// mode.x:
//   0  a stage sampling tex0 with the CPU-computed coordinates and colours
//   1  a lightmap stage: the preview light per pixel, as a lightmap texel
//   2  a lightmap stage where lightmaps do not apply (white)
//   3  the fog scene's walls: a neutral checker, writing distance to target 1
//      (the fog curve itself is material_screen.frag)
//   5  the sky box: tex0..tex5 are its rt bk lf ft up dn faces
//   6  a cloud stage over the sky, coordinates and colours per pixel
// mode.y: alpha test (0 none, 1 GT0, 2 LT128, 3 GE128).
// mode.z: bit 0 bilinear filtering.

#include "material_common.glsl"

VIBE_VARYING_IN(0) vec3 vPosition;
VIBE_VARYING_IN(1) vec3 vNormal;
VIBE_VARYING_IN(2) vec3 vTangent;
VIBE_VARYING_IN(3) vec3 vBitangent;
VIBE_VARYING_IN(4) vec4 vTexCoord;
VIBE_VARYING_IN(5) vec4 vColor;
VIBE_VARYING_IN(6) float vDepth;

VIBE_FRAGMENT_OUT(0) vec4 outColor;
VIBE_FRAGMENT_OUT(1) float outDistance;

// tcMods for a cloud layer, worked out per pixel: params[0..3] hold up to
// four (kind, a, b, c) entries with their matrices in matrices[0..7].
// Kind 1 is turbulence (a amplitude, b the moment); kind 2 an affine map.
vec2 cloudTexMods(vec2 st, vec3 position)
{
	for (int index = 0; index < 4; ++index) {
		vec4 entry = params[index];
		int kind = int(entry.x + 0.5);
		if (kind == 1) {
			float now = entry.z;
			float amplitude = entry.y;
			float s = st.x + quake3Sin(int(((position.x + position.z) / 1024.0 + now) * 1024.0)) * amplitude;
			float t = st.y + quake3Sin(int((position.y / 1024.0 + now) * 1024.0)) * amplitude;
			st = vec2(s, t);
		} else if (kind == 2) {
			vec4 row0 = matrices[index * 2];
			vec4 row1 = matrices[index * 2 + 1];
			st = vec2(row0.x * st.x + row0.y * st.y + row0.z, row1.x * st.x + row1.y * st.y + row1.z);
		}
	}
	return st;
}

void main()
{
	int program = mode.x;
	bool bilinear = (mode.z & 1) != 0;
	outDistance = vDepth;
	if (program == 3) {
		// The fog volume's walls: a neutral checker at identity light.
		vec2 st = vTexCoord.xy;
		bool lighter = wrapIndex(int(floor(st.x * 4.0)) + int(floor(st.y * 4.0)), 2) == 0;
		float value = (lighter ? 0.62 : 0.42) * viewport.w;
		outColor = vec4(value, value, value, 1.0);
		return;
	}
	if (program == 5) {
		outColor = vec4(skyBox(pixelRay(gl_FragCoord.xy)).rgb * viewport.w, 1.0);
		return;
	}
	if (program == 6) {
		// Clouds: R_InitSkyTexCoords on a sphere of radius 4096 at the cloud
		// height; none below the horizon box face.
		vec3 d = pixelRay(gl_FragCoord.xy);
		if (abs(d.z) >= max(abs(d.x), abs(d.y)) && d.z < 0.0) {
			discard;
		}
		float radius = 4096.0;
		float height = params[4].x;
		float dd = dot(d, d);
		float p = (-2.0 * radius * d.z + 2.0 * sqrt(radius * radius * d.z * d.z + dd * (2.0 * radius * height + height * height))) / (2.0 * dd);
		vec3 n = normalize(d * p + vec3(0.0, 0.0, radius));
		vec3 cloud = d * p;
		vec2 st = vec2(acos(clamp(n.x, -1.0, 1.0)), acos(clamp(n.y, -1.0, 1.0)));
		int source = int(params[4].y + 0.5);
		if (source == 1) {
			st = vec2(0.0);
		} else if (source == 2) {
			vec3 normal = vec3(0.0, 0.0, -1.0);
			vec3 viewer = normalize(eye.xyz - cloud);
			float facing = dot(normal, viewer);
			vec3 reflected = normal * (2.0 * facing) - viewer;
			st = vec2(0.5 + reflected.y * 0.5, 0.5 - reflected.z * 0.5);
		} else if (source == 3) {
			st = vec2(dot(cloud, params[5].xyz), dot(cloud, params[6].xyz));
		}
		st = cloudTexMods(st, cloud);
		vec4 fragment = sampleSlot(0, st, bilinear, 0.0);
		vec4 tint = color;
		int rgb = mode.w & 15;
		int alpha = (mode.w >> 4) & 15;
		if (rgb >= 1 && rgb <= 3) {
			vec4 lightmap = quake3Lightmap(previewLight(cloud, vec3(0.0, 0.0, -1.0)));
			float scale = rgb == 2 ? 1.0 : viewport.w;
			tint.rgb = (rgb == 3 ? vec3(1.0) - lightmap.rgb : lightmap.rgb) * scale;
		} else if (rgb == 4) {
			vec3 toLight = normalize(light.xyz - cloud);
			float facing = dot(vec3(0.0, 0.0, -1.0), toLight);
			float value = facing <= 0.0 ? params[7].x : min(255.0, params[7].x + facing * params[7].y);
			tint.rgb = value / 255.0 * lightColor.rgb;
		}
		if (alpha == 1) {
			vec3 toLight = normalize(vec3(-960.0, 1980.0, 96.0) - cloud);
			vec3 normal = vec3(0.0, 0.0, -1.0);
			vec3 reflected = normal * (2.0 * dot(normal, toLight)) - toLight;
			float l = dot(reflected, normalize(eye.xyz - cloud));
			tint.a = l < 0.0 ? 0.0 : min(1.0, l * l * l * l);
		} else if (alpha == 2) {
			tint.a = clamp(length(eye.xyz - cloud) / max(1.0, params[7].z), 0.0, 1.0);
		}
		fragment *= tint;
		if (!alphaPasses(mode.y, fragment.a)) {
			discard;
		}
		outColor = fragment;
		return;
	}
	vec4 fragment;
	if (program == 1) {
		fragment = quake3Lightmap(previewLight(vPosition, normalize(vNormal)));
	} else if (program == 2) {
		fragment = vec4(1.0);
	} else {
		vec3 toEye = normalize(eye.xyz - vPosition);
		float obliquity = abs(dot(normalize(vNormal), toEye));
		float lod = bilinear ? mipLevel(vDepth, obliquity, lighting.w) : 0.0;
		fragment = sampleSlot(0, vTexCoord.xy, bilinear, lod);
	}
	fragment *= vColor;
	if (!alphaPasses(mode.y, fragment.a)) {
		discard;
	}
	outColor = fragment;
}
