// Wireframe edge coverage: the stroke integrated over each square pixel,
// with square caps and the selected-edge dash pattern anchored at the
// leftmost (then topmost) endpoint, so a pattern never reverses with the
// direction an edge happens to be stored in. Output is premultiplied.

#include "common.glsl"

VIBE_UNIFORMS {
	mat4 viewProjection;
	vec4 viewport;
	vec4 wireColor;
	vec4 selectionColor;
	vec4 widths;
};

VIBE_FLAT_IN(0) vec4 vEnds;
VIBE_FLAT_IN(1) vec2 vStroke;

VIBE_FRAGMENT_OUT(0) vec4 outColor;

void main()
{
	vec2 a = vEnds.xy;
	vec2 b = vEnds.zw;
	float width = vStroke.x;
	bool selected = vStroke.y > 0.5;
	float radius = width * 0.5;
	vec2 point = gl_FragCoord.xy;
	vec2 delta = b - a;
	float segmentLength = sqrt(dot(delta, delta));
	vec2 unit = segmentLength > 0.0 ? delta / segmentLength : vec2(1.0, 0.0);
	// The stroke's direction in its major-axis frame.
	float major = max(abs(unit.x), abs(unit.y));
	float minorNormal = min(abs(unit.x), abs(unit.y));
	float halfSupport = (major + minorNormal) * 0.5;
	float shoulder = (major - minorNormal) * 0.5;
	float inverseMajor = 1.0 / major;
	float inverseArea = minorNormal > 0.0 ? 0.5 / (major * minorNormal) : 0.0;
	float reach = radius + halfSupport;
	vec2 offset = point - a;
	float along = dot(offset, unit);
	float across = abs(offset.x * unit.y - offset.y * unit.x);

	float nearBoundary = radius - across;
	if (nearBoundary <= -halfSupport) {
		discard;
	}
	float coverage = 1.0;
	if (nearBoundary < halfSupport) {
		if (nearBoundary >= shoulder) {
			float tail = halfSupport - nearBoundary;
			coverage = 1.0 - tail * tail * inverseArea;
		} else if (nearBoundary <= -shoulder) {
			float tail = halfSupport + nearBoundary;
			coverage = tail * tail * inverseArea;
		} else {
			coverage = 0.5 + nearBoundary * inverseMajor;
		}
	}
	float farBoundary = radius + across;
	if (farBoundary < halfSupport) {
		if (farBoundary >= shoulder) {
			float tail = halfSupport - farBoundary;
			coverage -= tail * tail * inverseArea;
		} else {
			coverage -= 0.5 - farBoundary * inverseMajor;
		}
	}
	coverage = min(coverage, reach + along);
	coverage = min(coverage, reach + segmentLength - along);
	float dashLength = width * widths.z;
	float dashPeriod = width * (widths.z + widths.w);
	if (selected && widths.z > 0.0 && segmentLength > dashLength && coverage > 0.0) {
		bool fromEnd = a.x > b.x || (a.x == b.x && a.y > b.y);
		float phase = mod(clamp(fromEnd ? segmentLength - along : along, 0.0, segmentLength), dashPeriod);
		if (phase > dashLength) {
			coverage = min(coverage, reach - min(phase - dashLength, dashPeriod - phase));
		}
	}
	if (coverage <= 0.0) {
		discard;
	}
	float factor = floor(min(coverage, 1.0) * 255.0 + 0.5) / 255.0;
	outColor = (selected ? selectionColor : wireColor) * factor;
}
