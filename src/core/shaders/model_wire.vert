// Wireframe edges: one instance per world-space segment, drawn as a
// screen-space quad wide enough for its antialiased stroke. Segments are
// clipped to the near plane before projection, so an edge passing beside or
// behind the camera still draws its visible part.

#include "common.glsl"

VIBE_ATTRIBUTE(0) vec3 aStart;
VIBE_ATTRIBUTE(1) vec3 aEnd;
// 1: a selected edge, dashed in the selection colour.
VIBE_ATTRIBUTE(2) uint aSelected;

VIBE_UNIFORMS {
	mat4 viewProjection;
	// xy: target size in pixels; z: the near plane in clip w, or a large
	// negative value for orthographic views.
	vec4 viewport;
	vec4 wireColor;
	vec4 selectionColor;
	// x: ordinary width, y: selected width (pixels); z: dash length and
	// w: dash gap, in stroke widths.
	vec4 widths;
};

// Endpoints in pixels from the top-left.
VIBE_FLAT_OUT(0) vec4 vEnds;
// x: stroke width, y: 1 when selected.
VIBE_FLAT_OUT(1) vec2 vStroke;

void main()
{
	bool selected = aSelected != 0u;
	float width = selected ? widths.y : widths.x;
	vStroke = vec2(width, selected ? 1.0 : 0.0);
	vec4 a = viewProjection * vec4(aStart, 1.0);
	vec4 b = viewProjection * vec4(aEnd, 1.0);
	float nearW = viewport.z;
	if (a.w < nearW && b.w < nearW) {
		vEnds = vec4(0.0);
		gl_Position = VIBE_DISCARDED_POSITION;
		return;
	}
	if (a.w < nearW) {
		a = mix(a, b, (nearW - a.w) / (b.w - a.w));
	} else if (b.w < nearW) {
		b = mix(b, a, (nearW - b.w) / (a.w - b.w));
	}
	vec2 start = (a.xy / a.w * 0.5 + 0.5) * viewport.xy;
	vec2 end = (b.xy / b.w * 0.5 + 0.5) * viewport.xy;
	vEnds = vec4(start, end);
	vec2 delta = end - start;
	float segmentLength = sqrt(dot(delta, delta));
	vec2 direction = segmentLength > 0.0 ? delta / segmentLength : vec2(1.0, 0.0);
	vec2 normal = vec2(-direction.y, direction.x);
	// The stroke's radius plus the widest pixel footprint (half a diagonal).
	float reach = width * 0.5 + 0.75;
	int corner = VIBE_VERTEX_INDEX % 6;
	// Two triangles: (0, 1, 2) and (2, 1, 3) of the quad's corners.
	int quadCorner = corner < 3 ? corner : (corner == 3 ? 2 : (corner == 4 ? 1 : 3));
	float along = (quadCorner & 1) != 0 ? segmentLength + reach : -reach;
	float across = (quadCorner & 2) != 0 ? reach : -reach;
	vec2 pixel = start + direction * along + normal * across;
	vec2 ndc = pixel / viewport.xy * 2.0 - 1.0;
	gl_Position = VIBE_CLIP(vec4(ndc, 0.5, 1.0));
}
