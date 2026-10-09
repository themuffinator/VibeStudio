// Doom 3 materials, following draw_common.cpp and tr_render.cpp: the depth
// fill, one interaction a bump/diffuse/specular group for the preview light,
// the remaining stages with their texture matrices and texgens, and light
// materials projected onto a neutral test surface.
//
// mode.x:
//   0  the depth fill: black; with flag 1 (perforated) only where one of the
//      mode.w alpha-tested stages in samplerInfo 0..3 passes (params[k].x is its
//      threshold, params[k].y its colour's alpha)
//   1  an interaction (see interaction() for its flags)
//   2  a regular stage (see stage())
//   3  a light material's stage over the test surface

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

// R_DeriveLightData's box: light-space coordinates for a point.
vec3 lightCoordinates(vec3 position)
{
	vec3 local = position - light.xyz;
	vec3 radius = params[0].xyz;
	return vec3(0.5 + local.x / (2.0 * radius.x), 0.5 + local.y / (2.0 * radius.y), 0.5 + local.z / (2.0 * radius.z));
}

// Projection image (slot 3, matrix 3) times falloff image (slot 4), or a
// soft round spot and _quadratic when there are none.
float attenuation(vec3 position, bool projection, bool falloff)
{
	vec3 c = lightCoordinates(position);
	float projected = 0.0;
	if (projection) {
		vec3 sampled = sampleSlot(3, applyMatrix(3, c.xy), true, 0.0).rgb;
		projected = (sampled.r + sampled.g + sampled.b) / 3.0;
	} else {
		float dx = c.x - 0.5;
		float dy = c.y - 0.5;
		float d2 = (dx * dx + dy * dy) * 4.0;
		projected = (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) ? 0.0 : clamp(1.0 - d2, 0.0, 1.0);
	}
	float fall = 0.0;
	if (falloff) {
		fall = sampleSlotWrapped(4, vec2(c.z, 0.5), true, 0.0, 2).r;
	} else {
		float x = c.z * 32.0;
		float d = max(0.0, abs(x - 15.5) - 0.5);
		fall = (c.z < 0.0 || c.z > 1.0) ? 0.0 : pow(max(0.0, 1.0 - d / 16.0), 2.0);
	}
	return projected * fall;
}

vec3 tint(vec3 position, bool projection)
{
	vec3 lightColour = params[1].rgb;
	if (!projection) {
		return lightColour;
	}
	vec3 c = lightCoordinates(position);
	vec3 sampled = sampleSlot(3, applyMatrix(3, c.xy), true, 0.0).rgb;
	float mean = max(1.0e-6, (sampled.r + sampled.g + sampled.b) / 3.0);
	return lightColour * sampled / mean;
}

vec3 tangentSpace(vec3 direction, vec3 tangent, vec3 bitangent, vec3 normal)
{
	return normalize(vec3(dot(direction, tangent), dot(direction, bitangent), dot(direction, normal)));
}

// Flags: 1 projection image, 2 falloff image, 4 bump (slot 0, matrix 0),
// 8 diffuse (slot 1, matrix 1), 16 specular (slot 2, matrix 2), 32 specular
// lighting on, 64 BFG shading, 128 inverse vertex colour, 256 add the
// ambient light (params[4].x its level). params[2] and params[3] are the
// diffuse and specular stage colours.
vec3 shade(bool ambientLight)
{
	vec3 position = vPosition;
	bool projection = flag(1) && !ambientLight;
	float lit = attenuation(position, projection, flag(2));
	if (lit <= 0.0) {
		return vec3(0.0);
	}
	vec3 lightColour = tint(position, projection);
	vec3 normal = normalize(vNormal);
	vec3 tangent = normalize(vTangent);
	vec3 bitangent = normalize(vBitangent);
	vec2 st = vTexCoord.xy;
	vec3 bumped = vec3(0.0, 0.0, 1.0);
	if (flag(4)) {
		bumped = normalize(sampleSlot(0, applyMatrix(0, st), true, 0.0).rgb * 2.0 - 1.0);
	}
	vec3 toLight = normalize(light.xyz - position);
	vec3 toEye = normalize(eye.xyz - position);
	vec3 l = tangentSpace(toLight, tangent, bitangent, normal);
	if (ambientLight) {
		// Vanilla ambient lights read a fixed direction from _ambient.
		l = normalize(vec3(0.0, -0.77, 0.785));
	}
	vec3 v = tangentSpace(toEye, tangent, bitangent, normal);
	vec3 h = normalize(l + v);
	float nDotL = dot(bumped, l);
	if (nDotL <= 0.0) {
		return vec3(0.0);
	}
	float nDotH = max(0.0, dot(bumped, h));
	vec3 diffuse = vec3(0.0);
	if (flag(8)) {
		diffuse = sampleSlot(1, applyMatrix(1, st), true, 0.0).rgb * params[2].rgb;
	}
	vec3 specular = vec3(0.0);
	if (flag(16) && flag(32) && !ambientLight) {
		vec3 map = sampleSlot(2, applyMatrix(2, st), true, 0.0).rgb;
		float term = 0.0;
		if (flag(64)) {
			term = pow(nDotH, 10.0) * 2.0;
		} else {
			// _specularTable, then the specular map doubled.
			float f = max(0.0, 4.0 * nDotH - 3.0);
			term = f * f * 2.0;
		}
		specular = map * params[3].rgb * term;
	}
	float vertex = flag(128) ? 0.0 : 1.0;
	return (diffuse + specular) * (nDotL * lit * vertex) * lightColour;
}

vec4 interaction()
{
	vec3 added = shade(false);
	if (flag(256)) {
		vec3 base = shade(true);
		float level = params[4].x / max(1.0e-6, attenuation(vPosition, false, flag(2)));
		added += base * min(4.0, level);
	}
	return vec4(added, 0.0);
}

// mode.w texgen: 0 the stage's coordinates, 1 normal, 2 reflect, 3 skybox,
// 4 wobblesky (params[0..2] its rotation rows), 5 screen. Flags: 1 bilinear,
// 2 a cube map in samplerInfo 0..5, 4 _currentRender (the frame so far, slot 7),
// 8 inverse vertex colour, 16 a 2D image in slot 0. The texture matrix is
// matrix 6; lighting.w the image's texels a world unit.
vec4 stage()
{
	vec3 position = vPosition;
	vec3 normal = normalize(vNormal);
	vec3 view = normalize(position - eye.xyz);
	bool bilinear = flag(1);
	int source = mode.w;
	vec4 fragment = vec4(0.0, 0.0, 0.0, 1.0);
	if (source >= 1 && source <= 4) {
		vec3 direction = normal;
		if (source == 2) {
			direction = view - normal * (2.0 * dot(normal, view));
		} else if (source == 3) {
			direction = position - eye.xyz;
		} else if (source == 4) {
			vec3 d = position - eye.xyz;
			direction = vec3(dot(d, params[0].xyz), dot(d, params[1].xyz), dot(d, params[2].xyz));
		}
		if (flag(2)) {
			fragment = cubeMap(direction);
		} else if (flag(16)) {
			vec3 unit = normalize(direction);
			fragment = sampleSlot(0, vec2(0.5 + unit.y * 0.5, 0.5 - unit.z * 0.5), bilinear, 0.0);
		}
	} else {
		vec2 st = vTexCoord.xy;
		bool renderTarget = flag(4);
		if (source == 5 || renderTarget) {
			st = vec2(gl_FragCoord.x / viewport.x, 1.0 - gl_FragCoord.y / viewport.y);
		}
		st = applyMatrix(6, st);
		if (renderTarget) {
			ivec2 size = ivec2(viewport.xy);
			int x = clamp(int(st.x * float(size.x)), 0, size.x - 1);
			int y = clamp(int((1.0 - st.y) * float(size.y)), 0, size.y - 1);
			fragment = vec4(texelFetch(tex7, ivec2(x, y), 0).rgb, 1.0);
		} else if (flag(2)) {
			fragment = cubeMap(normal);
		} else if (flag(16)) {
			float obliquity = abs(dot(normal, view));
			float lod = bilinear ? mipLevel(vDepth, obliquity, lighting.w) : 0.0;
			fragment = sampleSlot(0, st, bilinear, lod);
		}
	}
	fragment *= color;
	if (flag(8)) {
		fragment = vec4(0.0);
	}
	return fragment;
}

// Flags: 1 projection image, 2 falloff image, 4 an ambient light (no
// facing), 8 a blend light (drawn with the stage's own blend).
vec4 lightStage()
{
	vec3 position = vPosition;
	bool projection = flag(1);
	float lit = attenuation(position, projection, flag(2));
	if (lit <= 0.0) {
		discard;
	}
	vec3 colour = tint(position, projection);
	if (flag(8)) {
		return vec4(colour / 2.0 * lit, 1.0);
	}
	vec3 toLight = normalize(light.xyz - position);
	float nDotL = flag(4) ? 1.0 : max(0.0, dot(normalize(vNormal), toLight));
	// The test surface: mid grey, a faint checker so motion reads.
	vec2 st = vTexCoord.xy;
	bool lighter = wrapIndex(int(floor(st.x * 4.0)) + int(floor(st.y * 4.0)), 2) == 0;
	float albedo = lighter ? 0.55 : 0.45;
	return vec4(colour * (albedo * nDotL * lit), 0.0);
}

void main()
{
	int program = mode.x;
	if (program == 0) {
		if (flag(1)) {
			bool passed = false;
			for (int k = 0; k < 4; ++k) {
				if (k >= mode.w) {
					break;
				}
				float alpha = sampleSlot(k, applyMatrix(k, vTexCoord.xy), true, 0.0).a * params[k].y;
				if (alpha > params[k].x) {
					passed = true;
					break;
				}
			}
			if (!passed) {
				discard;
			}
		}
		outColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	if (program == 1) {
		outColor = interaction();
		return;
	}
	if (program == 2) {
		outColor = stage();
		return;
	}
	outColor = lightStage();
}
